#pragma once
#include <volk/volk.h>
#include <vector>

struct SDL_Window;

struct Color { float r, g, b, a; };

class VulkanContext {
public:
    bool Init(SDL_Window* window);
    void AddCircle(float cx, float cy, float radius, Color color);
    void RenderFrame();
    void Shutdown();

private:
    struct CircleData { float cx, cy, radius; Color color; };
    struct CirclePushConstants {
        float r, g, b, a;    // vec4  color
        float ndcCx, ndcCy;  // vec2  NDC center
        float ndcRx, ndcRy;  // vec2  NDC half-extents (aspect-correct)
        float radius;        // float pixel radius for SDF
    };

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

    VkPipelineLayout circlePipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       circlePipeline{ VK_NULL_HANDLE };

    std::vector<CircleData> circles;

    void CreateSwapchain();
    void RecreateSwapchain();
    void CreatePipeline();
};