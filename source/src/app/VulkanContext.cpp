#define VOLK_IMPLEMENTATION
#include <volk/volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "app/VulkanContext.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <vector>
#include <cstdio>
#include <cstring>

static void chk(VkResult r) {
    if (r != VK_SUCCESS) { std::cerr << "Vulkan error: " << r << "\n"; exit(1); }
}

static std::vector<uint32_t> LoadSpv(const char* name) {
    static std::string base = [] {
        const char* b = SDL_GetBasePath();
        return std::string(b ? b : "");
    }();
    std::string path = base + "shaders/" + name;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "Cannot open shader: %s\n", path.c_str()); exit(1); }
    fseek(f, 0, SEEK_END); size_t size = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> buf(size / 4);
    fread(buf.data(), 1, size, f); fclose(f);
    return buf;
}

static VkShaderModule MakeShader(VkDevice device, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo ci{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = code.size() * 4, .pCode = code.data() };
    VkShaderModule mod; chk(vkCreateShaderModule(device, &ci, nullptr, &mod)); return mod;
}

bool VulkanContext::Init(SDL_Window* window) {
    sdlWindow = window;
    volkInitialize();

    // Instance
    VkApplicationInfo appInfo{ .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Stjarna", .apiVersion = VK_API_VERSION_1_3 };
    uint32_t extCount = 0;
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    VkInstanceCreateInfo instanceCI{ .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &appInfo, .enabledExtensionCount = extCount, .ppEnabledExtensionNames = exts };
    chk(vkCreateInstance(&instanceCI, nullptr, &instance));
    volkLoadInstance(instance);

    // Surface
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << "\n"; return false;
    }

    // Physical device
    uint32_t deviceCount = 0;
    chk(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
    if (deviceCount == 0) { std::cerr << "No Vulkan-capable GPU found\n"; return false; }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    chk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
    physicalDevice = devices[0];

    // Queue family
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qfs.data());
    bool found = false;
    for (uint32_t i = 0; i < qfCount; i++) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &present);
        if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { queueFamily = i; found = true; break; }
    }
    if (!found) { std::cerr << "No graphics+present queue family\n"; return false; }

    // Logical device with dynamic rendering
    const float qp = 1.0f;
    VkDeviceQueueCreateInfo queueCI{ .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = queueFamily, .queueCount = 1, .pQueuePriorities = &qp };
    const char* devExts[]{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceDynamicRenderingFeatures dynFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, .dynamicRendering = VK_TRUE };
    VkDeviceCreateInfo deviceCI{ .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &dynFeatures, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queueCI, .enabledExtensionCount = 1, .ppEnabledExtensionNames = devExts };
    chk(vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device));
    volkLoadDevice(device);
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    // Surface format
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    imageFormat = formats[0].format; colorSpace = formats[0].colorSpace;
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
            { imageFormat = f.format; colorSpace = f.colorSpace; break; }

    CreateSwapchain();

    // Shared descriptor set layout: one SSBO at binding 0, vertex stage
    VkDescriptorSetLayoutBinding ssboBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dslCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &ssboBinding,
    };
    chk(vkCreateDescriptorSetLayout(device, &dslCI, nullptr, &shapeDescSetLayout));

    CreateShapePipeline("circle.vert.spv", "circle.frag.spv", shapeDescSetLayout, circlePipelineLayout, circlePipeline);
    CreateShapePipeline("rect.vert.spv",   "rect.frag.spv",   shapeDescSetLayout, rectPipelineLayout,   rectPipeline);

    // SSBOs — persistently mapped, host-visible + coherent
    const VkDeviceSize circleSSBOSize = kMaxObjects * sizeof(CircleData);
    const VkDeviceSize rectSSBOSize   = kMaxObjects * sizeof(RectData);
    CreateSSBO(circleSSBOSize, circleSSBO, circleSSBOMemory, circleMapped);
    CreateSSBO(rectSSBOSize,   rectSSBO,   rectSSBOMemory,   rectMapped);

    // Descriptor pool + sets
    VkDescriptorPoolSize poolSize{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 };
    VkDescriptorPoolCreateInfo poolCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &poolSize,
    };
    chk(vkCreateDescriptorPool(device, &poolCI, nullptr, &descPool));

    VkDescriptorSetLayout layouts[2] = { shapeDescSetLayout, shapeDescSetLayout };
    VkDescriptorSetAllocateInfo dsAllocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descPool, .descriptorSetCount = 2, .pSetLayouts = layouts,
    };
    VkDescriptorSet sets[2];
    chk(vkAllocateDescriptorSets(device, &dsAllocInfo, sets));
    circleDescSet = sets[0];
    rectDescSet   = sets[1];

    VkDescriptorBufferInfo circleBI{ .buffer = circleSSBO, .offset = 0, .range = VK_WHOLE_SIZE };
    VkDescriptorBufferInfo rectBI  { .buffer = rectSSBO,   .offset = 0, .range = VK_WHOLE_SIZE };
    VkWriteDescriptorSet writes[2]{
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = circleDescSet, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &circleBI },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = rectDescSet,   .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &rectBI   },
    };
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

    VkCommandPoolCreateInfo cpCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = queueFamily };
    chk(vkCreateCommandPool(device, &cpCI, nullptr, &commandPool));
    VkCommandBufferAllocateInfo cbAI{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool, .commandBufferCount = 1 };
    chk(vkAllocateCommandBuffers(device, &cbAI, &cb));

    VkSemaphoreCreateInfo semCI{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceCI{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    chk(vkCreateSemaphore(device, &semCI, nullptr, &acquireSem));
    chk(vkCreateSemaphore(device, &semCI, nullptr, &renderSem));
    chk(vkCreateFence(device, &fenceCI, nullptr, &fence));

    circles.reserve(kMaxObjects);
    rects.reserve(kMaxObjects);
    return true;
}

void VulkanContext::InitImGui(SDL_Window* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.WindowBorderSize  = 0.0f;

    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion      = VK_API_VERSION_1_3;
    info.Instance        = instance;
    info.PhysicalDevice  = physicalDevice;
    info.Device          = device;
    info.QueueFamily     = queueFamily;
    info.Queue           = queue;
    info.DescriptorPoolSize = 16;
    info.MinImageCount   = 2;
    info.ImageCount      = (uint32_t)images.size();
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &imageFormat,
    };
    ImGui_ImplVulkan_Init(&info);
}

void VulkanContext::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(sdlWindow, &w, &h);
    swapchainExtent = caps.currentExtent.width == 0xFFFFFFFF ? VkExtent2D{ (uint32_t)w, (uint32_t)h } : caps.currentExtent;
    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

    for (auto& iv : imageViews) vkDestroyImageView(device, iv, nullptr);
    imageViews.clear();

    VkSwapchainKHR old = swapchain;
    VkSwapchainCreateInfoKHR swapCI{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface,
        .minImageCount = imgCount, .imageFormat = imageFormat, .imageColorSpace = colorSpace,
        .imageExtent = swapchainExtent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .oldSwapchain = old,
    };
    chk(vkCreateSwapchainKHR(device, &swapCI, nullptr, &swapchain));
    if (old != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, old, nullptr);

    uint32_t imageCount = 0;
    chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
    images.resize(imageCount);
    chk(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data()));

    imageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo ivCI{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = imageFormat,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        chk(vkCreateImageView(device, &ivCI, nullptr, &imageViews[i]));
    }
}

void VulkanContext::RecreateSwapchain() {
    vkDeviceWaitIdle(device);
    CreateSwapchain();
}

void VulkanContext::CreateShapePipeline(const char* vertSpv, const char* fragSpv,
                                        VkDescriptorSetLayout descSetLayout,
                                        VkPipelineLayout& outLayout, VkPipeline& outPipeline) {
    VkShaderModule vertMod = MakeShader(device, LoadSpv(vertSpv));
    VkShaderModule fragMod = MakeShader(device, LoadSpv(fragSpv));

    VkPipelineShaderStageCreateInfo stages[2]{
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,   .module = vertMod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragMod, .pName = "main" },
    };
    VkPipelineVertexInputStateCreateInfo   vertexInput  { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo      viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rasterizer   { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo   multisample  { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState blendAtt{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,       .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,               .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo colorBlend  { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAtt };
    VkDynamicState dynStates[]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo    dynamicState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynStates };

    VkPushConstantRange pcRange{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = 2 * sizeof(float) };
    VkPipelineLayoutCreateInfo layoutCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &descSetLayout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &pcRange,
    };
    chk(vkCreatePipelineLayout(device, &layoutCI, nullptr, &outLayout));

    VkPipelineRenderingCreateInfo renderingCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &imageFormat };
    VkGraphicsPipelineCreateInfo pipelineCI{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &renderingCI,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vertexInput, .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,  .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisample, .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,    .layout = outLayout,
    };
    chk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &outPipeline));

    vkDestroyShaderModule(device, vertMod, nullptr);
    vkDestroyShaderModule(device, fragMod, nullptr);
}

void VulkanContext::CreateSSBO(VkDeviceSize size, VkBuffer& buf, VkDeviceMemory& mem, void*& mapped) {
    VkBufferCreateInfo bCI{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    chk(vkCreateBuffer(device, &bCI, nullptr, &buf));

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, buf, &memReq);

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    uint32_t memTypeIndex = UINT32_MAX;
    const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((memReq.memoryTypeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & required) == required) {
            memTypeIndex = i; break;
        }
    }
    if (memTypeIndex == UINT32_MAX) { std::cerr << "No suitable memory type for SSBO\n"; exit(1); }

    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReq.size,
        .memoryTypeIndex = memTypeIndex,
    };
    chk(vkAllocateMemory(device, &allocInfo, nullptr, &mem));
    chk(vkBindBufferMemory(device, buf, mem, 0));
    chk(vkMapMemory(device, mem, 0, VK_WHOLE_SIZE, 0, &mapped));
}

void VulkanContext::AddCircle(float cx, float cy, float radius, Color color) {
    if (circles.size() >= kMaxObjects) return;
    circles.push_back({ color.r, color.g, color.b, color.a, cx, cy, radius, 0.0f });
}

void VulkanContext::AddRectangle(float cx, float cy, float halfW, float halfH, Color color) {
    if (rects.size() >= kMaxObjects) return;
    rects.push_back({ color.r, color.g, color.b, color.a, cx, cy, halfW, halfH });
}

void VulkanContext::RenderFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (profilerOpen_) {
        ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("Profiler", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "%.0f fps", profilerStats_.fps);
        ImGui::Separator();
        ImGui::Text("frame   %6.2f ms", profilerStats_.frameMs);
        ImGui::Text("compute %6.2f ms", profilerStats_.computeMs);
        ImGui::Text("render  %6.2f ms", profilerStats_.renderMs);
        ImGui::Separator();
        ImGui::Text("objects %d", objectCount_);
        ImGui::Separator();
        ImGui::TextDisabled("F1 to close");
        ImGui::End();
    }

    ImGui::Render();

    chk(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquireSem, VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) { RecreateSwapchain(); circles.clear(); rects.clear(); return; }
    if (acq != VK_SUCCESS) chk(acq);

    chk(vkResetFences(device, 1, &fence));
    chk(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cbbi{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    chk(vkBeginCommandBuffer(cb, &cbbi));

    VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkImageMemoryBarrier toRender{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = images[imageIndex], .subresourceRange = range,
    };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRender);

    VkRenderingAttachmentInfo colorAtt{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = imageViews[imageIndex], .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { .float32 = { 0.07f, 0.08f, 0.12f, 1.0f } } },
    };
    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0,0}, swapchainExtent },
        .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &colorAtt,
    };
    vkCmdBeginRendering(cb, &renderingInfo);

    {
        VkViewport viewport{ 0, 0, (float)swapchainExtent.width, (float)swapchainExtent.height, 0.0f, 1.0f };
        VkRect2D   scissor{ {0,0}, swapchainExtent };
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);

        float invScreen[2] = { 2.0f / swapchainExtent.width, 2.0f / swapchainExtent.height };

        if (!circles.empty()) {
            const uint32_t n = (uint32_t)circles.size();
            memcpy(circleMapped, circles.data(), n * sizeof(CircleData));
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, circlePipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, circlePipelineLayout, 0, 1, &circleDescSet, 0, nullptr);
            vkCmdPushConstants(cb, circlePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(invScreen), invScreen);
            vkCmdDraw(cb, 6, n, 0, 0);
        }

        if (!rects.empty()) {
            const uint32_t n = (uint32_t)rects.size();
            memcpy(rectMapped, rects.data(), n * sizeof(RectData));
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, rectPipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, rectPipelineLayout, 0, 1, &rectDescSet, 0, nullptr);
            vkCmdPushConstants(cb, rectPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(invScreen), invScreen);
            vkCmdDraw(cb, 6, n, 0, 0);
        }
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

    vkCmdEndRendering(cb);
    circles.clear();
    rects.clear();

    VkImageMemoryBarrier toPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = images[imageIndex], .subresourceRange = range,
    };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    chk(vkEndCommandBuffer(cb));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &acquireSem, .pWaitDstStageMask = &waitStage, .commandBufferCount = 1, .pCommandBuffers = &cb, .signalSemaphoreCount = 1, .pSignalSemaphores = &renderSem };
    chk(vkQueueSubmit(queue, 1, &si, fence));

    VkPresentInfoKHR pi{ .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &renderSem, .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &imageIndex };
    VkResult presentResult = vkQueuePresentKHR(queue, &pi);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) RecreateSwapchain();
    else if (presentResult != VK_SUCCESS) chk(presentResult);
}

void VulkanContext::Shutdown() {
    vkDeviceWaitIdle(device);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    vkDestroyPipeline(device, rectPipeline, nullptr);
    vkDestroyPipelineLayout(device, rectPipelineLayout, nullptr);
    vkDestroyPipeline(device, circlePipeline, nullptr);
    vkDestroyPipelineLayout(device, circlePipelineLayout, nullptr);
    vkUnmapMemory(device, rectSSBOMemory);
    vkDestroyBuffer(device, rectSSBO, nullptr);
    vkFreeMemory(device, rectSSBOMemory, nullptr);
    vkUnmapMemory(device, circleSSBOMemory);
    vkDestroyBuffer(device, circleSSBO, nullptr);
    vkFreeMemory(device, circleSSBOMemory, nullptr);
    vkDestroyDescriptorPool(device, descPool, nullptr);
    vkDestroyDescriptorSetLayout(device, shapeDescSetLayout, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroySemaphore(device, renderSem, nullptr);
    vkDestroySemaphore(device, acquireSem, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    for (auto& iv : imageViews) vkDestroyImageView(device, iv, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
