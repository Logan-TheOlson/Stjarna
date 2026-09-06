#pragma once
#include <volk.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "Config.h"
#include "util/Profiler.h"
#include "util/Recorder.h"

struct SDL_Window;

class VulkanContext {
public:
    bool Init(SDL_Window* window);
    void InitImGui(SDL_Window* window);
    void AddCircle(float cx, float cy, float radius, Color color);
    // Draws a hollow rectangular frame (border), `borderThickness` pixels wide, centered at
    // (cx, cy) with the given half-extents — used for boundary markers, not particles.
    void AddRectOutline(float cx, float cy, float halfWidth, float halfHeight, float borderThickness, Color color);
    void RenderFrame(float dt);
    void Shutdown();
    void SetProfilerOpen(bool open)                 { profilerOpen_ = open; }
    void SetProfilerStats(const Profiler::Stats& s) { profilerStats_ = s; }
    void SetObjectCount(int n)                      { objectCount_ = n; }
    void SetKineticEnergy(float ke)                 { kineticEnergy_ = ke; }
    void SetUICallback(std::function<void()> cb)    { uiCallback_ = std::move(cb); }
    int   Width()      const { return (int)swapchainExtent.width; }
    int   Height()     const { return (int)swapchainExtent.height; }
    float HalfWidth()  const { return swapchainExtent.width  * 0.5f; }
    float HalfHeight() const { return swapchainExtent.height * 0.5f; }

    // Captures the composited (scene + UI) swapchain image at `fps`, pacing capture against real
    // time (via RenderFrame's dt) rather than the render loop's actual rate. lengthSeconds <= 0
    // means "record until StopRecording() is called".
    bool  StartRecording(const std::string& outputPath, int fps, float lengthSeconds);
    void  StopRecording();
    bool  IsRecording() const { return recorder_.IsActive(); }
    float RecordedSeconds() const { return recordFps_ > 0 ? static_cast<float>(recordedFrames_) / recordFps_ : 0.f; }

private:
    // SSBO instance data â€” world-space coords; NDC computed in vertex shader via push constants
    struct CircleData {
        float r, g, b, a;  // vec4 color  â€” offset  0
        float cx, cy;      // vec2 center â€” offset 16
        float radius;      //               offset 24
        float _pad;        //               offset 28, total 32
    };
    static_assert(sizeof(CircleData) == 32);

    // SSBO instance data for the hollow-frame rect shader (see shaders/rect.vert/.frag).
    struct RectData {
        float r, g, b, a;             // vec4 color  — offset  0
        float cx, cy;                 // vec2 rect.xy — offset 16
        float halfWidth, halfHeight;  // vec2 rect.zw — offset 24
        float borderThickness;        //               offset 32
        float _pad0, _pad1, _pad2;    //               offset 36..44, total 48
    };
    static_assert(sizeof(RectData) == 48);

    static constexpr uint32_t kMaxObjects = 100000;
    static constexpr uint32_t kMaxRects   = 64;

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
    VkDescriptorSet       rectDescSet{ VK_NULL_HANDLE };

    VkBuffer       circleSSBO{ VK_NULL_HANDLE };
    VkDeviceMemory circleSSBOMemory{ VK_NULL_HANDLE };
    void*          circleMapped{ nullptr };

    VkBuffer       rectSSBO{ VK_NULL_HANDLE };
    VkDeviceMemory rectSSBOMemory{ VK_NULL_HANDLE };
    void*          rectMapped{ nullptr };

    VkPipelineLayout circlePipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       circlePipeline{ VK_NULL_HANDLE };

    VkPipelineLayout rectPipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       rectPipeline{ VK_NULL_HANDLE };

    std::vector<CircleData> circles;
    std::vector<RectData>   rects;

    bool            profilerOpen_{ false };
    Profiler::Stats profilerStats_{};
    int             objectCount_{ 0 };
    float           kineticEnergy_{ 0.f };
    std::function<void()> uiCallback_;

    // Frame recording: the final composited image is copied into one of a small ring of
    // host-visible readback buffers. Recorder's background thread reads a slot directly (an
    // fread of GPU-visible memory over PCIe can be slow) and calls its release callback when
    // done, which is what frees that slot for reuse — so that slow read never sits on the render
    // thread's critical path. Only one GPU copy is ever in flight (this renderer has a single
    // frame in flight generally), so kCaptureSlots' only job is to give the background reader a
    // few frames of grace before a slot it's still draining is needed again; if it falls behind
    // regardless, capture is skipped for that frame rather than stalling to wait for it.
    static constexpr int kCaptureSlots = 3;

    Recorder     recorder_;
    int          recordFps_{ 0 };
    float        recordLengthSeconds_{ 0.f };
    float        recordAccum_{ 0.f };
    uint32_t     recordedFrames_{ 0 };
    uint32_t     recordWidth_{ 0 }, recordHeight_{ 0 };
    VkDeviceSize captureBufferSize_{ 0 };
    // Whether captureMemories_'s type is HOST_COHERENT; if not, CPU reads need an explicit
    // vkInvalidateMappedMemoryRanges first (see CreateSSBO's memory-type selection).
    bool         captureMemoryCoherent_{ true };

    VkBuffer          captureBuffers_[kCaptureSlots]{};
    VkDeviceMemory    captureMemories_[kCaptureSlots]{};
    void*             captureMapped_[kCaptureSlots]{};
    std::atomic<bool> captureSlotBusy_[kCaptureSlots]{};
    int               captureWriteIndex_{ 0 };
    int               pendingCaptureSlot_{ -1 }; // slot the most recent submission copied into, not yet handed to Recorder

    // Hands pendingCaptureSlot_ (if any) to Recorder and clears it. Only valid to call once the
    // GPU copy into that slot is known to have finished (i.e. after a fence wait or
    // vkDeviceWaitIdle) — called both per-frame and from StopRecording(), so a stop never drops
    // the frame the most recent RenderFrame() call just captured.
    void FlushPendingCapture();

    void CreateSwapchain();
    void RecreateSwapchain();
    void CreateShapePipeline(const char* vertSpv, const char* fragSpv,
                             VkDescriptorSetLayout descSetLayout,
                             VkPipelineLayout& outLayout, VkPipeline& outPipeline);
    // `preferred` flags are matched on top of HOST_VISIBLE where available (falling back to
    // plain HOST_VISIBLE|HOST_COHERENT otherwise); pass HOST_CACHED for a readback buffer the
    // CPU will read from — device memory that's HOST_VISIBLE|HOST_COHERENT but not cached is
    // typically an uncached PCIe-mapped region, and CPU reads from it can be an order of
    // magnitude slower than reads from normal (cached) system memory.
    void CreateSSBO(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags preferred,
                    VkBuffer& buf, VkDeviceMemory& mem, void*& mapped, bool* outCoherent = nullptr);
};
