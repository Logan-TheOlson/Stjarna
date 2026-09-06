#pragma once
#include <volk.h>
#include <vector>
#include "Config.h"
#include "util/Profiler.h"

struct SDL_Window;

class VulkanContext {
public:
    bool Init(SDL_Window* window);
    void InitImGui(SDL_Window* window);
    void AddCircle(float cx, float cy, float radius, Color color);
    void RenderFrame();
    void Shutdown();
    void SetProfilerOpen(bool open)                 { profilerOpen_ = open; }
    void SetProfilerStats(const Profiler::Stats& s) { profilerStats_ = s; }
    void SetObjectCount(int n)                      { objectCount_ = n; }
    void SetKineticEnergy(float ke)                 { kineticEnergy_ = ke; }
    void SetTotalEnergy(float e, bool valid)        { totalEnergy_ = e; totalEnergyValid_ = valid; }
    float TimeScale() const                         { return timeScale_; }
    int   Width()      const { return (int)swapchainExtent.width; }
    int   Height()     const { return (int)swapchainExtent.height; }
    float HalfWidth()  const { return swapchainExtent.width  * 0.5f; }
    float HalfHeight() const { return swapchainExtent.height * 0.5f; }

    // Precompute-mode frame readback: when enabled, RenderFrame() additionally
    // copies the just-rendered swapchain image into a CPU-visible staging
    // buffer and blocks until that copy completes, so the raw BGRA8 pixels are
    // safely readable via CapturedPixelData() immediately after RenderFrame()
    // returns. Off by default (adds a blocking wait per frame) — only meant
    // for the offline video-export path, not the interactive one.
    void EnableCapture(bool enabled);
    const void* CapturedPixelData() const     { return captureMapped_; }
    size_t      CapturedPixelDataSize() const { return static_cast<size_t>(swapchainExtent.width) * swapchainExtent.height * 4; }

private:
    // SSBO instance data â€” world-space coords; NDC computed in vertex shader via push constants
    struct CircleData {
        float r, g, b, a;  // vec4 color  â€” offset  0
        float cx, cy;      // vec2 center â€” offset 16
        float radius;      //               offset 24
        float _pad;        //               offset 28, total 32
    };
    static_assert(sizeof(CircleData) == 32);

    static constexpr uint32_t kMaxObjects = 100000;

    SDL_Window*      sdlWindow{ nullptr };

    VkInstance       instance{ VK_NULL_HANDLE };
    VkSurfaceKHR     surface{ VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
    VkDevice         device{ VK_NULL_HANDLE };
    VkQueue          queue{ VK_NULL_HANDLE };
    uint32_t         queueFamily{ 0 };

    VkFormat         imageFormat{ VK_FORMAT_UNDEFINED };
    VkColorSpaceKHR  colorSpace{ VK_COLORSPACE_SRGB_NONLINEAR_KHR };
    VkExtent2D       swapchainExtent{};

    VkSwapchainKHR         swapchain{ VK_NULL_HANDLE };
    std::vector<VkImage>   images;
    std::vector<VkImageView> imageViews;

    VkCommandPool   commandPool{ VK_NULL_HANDLE };
    VkCommandBuffer cb{ VK_NULL_HANDLE };

    VkSemaphore acquireSem{ VK_NULL_HANDLE };
    VkSemaphore renderSem{ VK_NULL_HANDLE };
    VkFence     fence{ VK_NULL_HANDLE };

    VkDescriptorSetLayout shapeDescSetLayout{ VK_NULL_HANDLE };
    VkDescriptorPool      descPool{ VK_NULL_HANDLE };
    VkDescriptorSet       circleDescSet{ VK_NULL_HANDLE };

    VkBuffer       circleSSBO{ VK_NULL_HANDLE };
    VkDeviceMemory circleSSBOMemory{ VK_NULL_HANDLE };
    void*          circleMapped{ nullptr };

    VkPipelineLayout circlePipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       circlePipeline{ VK_NULL_HANDLE };

    std::vector<CircleData> circles;

    bool            profilerOpen_{ false };
    Profiler::Stats profilerStats_{};
    int             objectCount_{ 0 };
    float           kineticEnergy_{ 0.f };
    float           totalEnergy_{ 0.f };
    bool            totalEnergyValid_{ false };
    float           timeScale_{ 1.f };

    bool            captureEnabled_{ false };
    VkBuffer        captureBuffer_{ VK_NULL_HANDLE };
    VkDeviceMemory  captureMemory_{ VK_NULL_HANDLE };
    void*           captureMapped_{ nullptr };

    void CreateSwapchain();
    void RecreateSwapchain();
    void CreateShapePipeline(const char* vertSpv, const char* fragSpv,
                             VkDescriptorSetLayout descSetLayout,
                             VkPipelineLayout& outLayout, VkPipeline& outPipeline);
    void CreateHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem, void*& mapped);
};
