#pragma once
#include <volk/volk.h>
#include <vector>
#include "../Config.h"
#include "../Profiler.h"

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
    int   Width()      const { return (int)swapchainExtent.width; }
    int   Height()     const { return (int)swapchainExtent.height; }
    float HalfWidth()  const { return swapchainExtent.width  * 0.5f; }
    float HalfHeight() const { return swapchainExtent.height * 0.5f; }

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

    bool            profilerOpen_{ false };
    Profiler::Stats profilerStats_{};
    int             objectCount_{ 0 };

    void CreateSwapchain();
    void RecreateSwapchain();
    void CreatePipeline();
};