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

    // The half-extents the simulation itself should measure its boundary against: pinned to the
    // active recording's fixed resolution (recordWidth_/recordHeight_) while one is in progress,
    // since that's the coordinate system the captured video frame actually uses — the live
    // swapchain can still change size underneath a batch render (see RecreateSwapchain), and
    // using HalfWidth()/HalfHeight() directly there would draw the boundary (and bounce particles
    // off walls) at a position that no longer matches where the fixed-size recorded frame puts
    // its own edges, splitting the border into visible stray lines partway through the video.
    // Live (non-batch) recording never actually hits this divergence — a swapchain resize there
    // stops the recording outright (see RecreateSwapchain) before hw/hh can drift — but pinning
    // unconditionally whenever IsRecording() is simpler than special-casing which capture mode.
    float RecordingHalfWidth()  const { return recorder_.IsActive() ? recordWidth_  * 0.5f : HalfWidth(); }
    float RecordingHalfHeight() const { return recorder_.IsActive() ? recordHeight_ * 0.5f : HalfHeight(); }

    // Captures the composited (scene + UI) swapchain image at `fps`, pacing capture against real
    // time (via RenderFrame's dt) rather than the render loop's actual rate. lengthSeconds <= 0
    // means "record until StopRecording() is called".
    bool  StartRecording(const std::string& outputPath, int fps, float lengthSeconds);
    void  StopRecording();
    bool  IsRecording() const { return recorder_.IsActive(); }
    float RecordedSeconds() const { return recordFps_ > 0 ? static_cast<float>(recordedFrames_) / recordFps_ : 0.f; }

    // Batch rendering (see Engine.cpp's AdvanceRendering) drives capture entirely through
    // RenderOffscreenFrame, but the main loop still calls RenderFrame once per real tick to keep
    // the progress overlay on screen. Without this flag RenderFrame's own capture gate would fire
    // too (recorder_.IsActive() is still true), copying that swapchain frame — which is just the
    // overlay, since circles/rects were already cleared by the offscreen pass — into the video as
    // a blank frame spliced in among the real ones. False while batch rendering; true otherwise.
    void  SetCaptureFromSwapchain(bool enable) { captureFromSwapchain_ = enable; }

    // Renders the scene (not UI) straight to an offscreen target and captures it, with no
    // swapchain/present involvement at all — unlike RenderFrame(), this can't be throttled by
    // vsync or by the OS deprioritizing an occluded/backgrounded window, since nothing is ever
    // shown on screen. Every call captures unconditionally (no fps-interval gating — the caller,
    // AdvanceRendering, already only calls this once per intended video frame) and auto-stops the
    // recording via the same length cap StartRecording() set up. A no-op if no recording is active.
    void RenderOffscreenFrame();

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

    // Sample count the density (circle) pass renders at, chosen once in Init() from what the
    // device actually supports (falls back to VK_SAMPLE_COUNT_1_BIT, i.e. no MSAA, if 4x isn't
    // available). This anti-aliases more than each shape's own edge (see circle.frag's smoothstep
    // rim, which already handled that) — at high particle counts, particles shrink to a couple of
    // pixels across and their arrangement (e.g. the Vogel/Fibonacci spiral MakeSelfGravity's
    // circular spawn uses) becomes a near-regular point lattice; sampling it at only one point per
    // pixel aliases that regularity into a visible moiré "screen" pattern that per-shape edge AA
    // can't touch. Multisampling the whole pass softens that by sampling coverage several times
    // per pixel before resolving down to one color, the same fix a renderer would use for any
    // dense, regular geometry — see densityMsaaImage/offscreenDensityMsaaImage below.
    VkSampleCountFlagBits sceneSampleCount{ VK_SAMPLE_COUNT_1_BIT };

    // Format of the density accumulation targets below — needs to hold sums well past 1.0 (many
    // overlapping particles at a "high pressure" cluster), which an 8-bit UNORM swapchain format
    // can't represent, hence a floating-point format distinct from imageFormat.
    static constexpr VkFormat kDensityFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkSwapchainKHR         swapchain{ VK_NULL_HANDLE };
    std::vector<VkImage>   images;
    std::vector<VkImageView> imageViews;

    // Circles no longer draw straight into the swapchain/offscreen target. Instead they accumulate
    // additively (premultiplied color + coverage, see circle.frag) into this float density buffer —
    // densityMsaaImage is the multisampled draw target (same anti-aliasing role sceneSampleCount's
    // comment describes, just aimed at this buffer instead), resolved into densityImage, which is
    // sampled back in the composite pass (see RecordCompositePass) to turn accumulated density into
    // final color: unpremultiplying it recovers the density-weighted *average* color wherever
    // several particles overlap, which is what makes dense clusters blend together instead of
    // layering as discrete translucent disks. densityMsaaImage is left at VK_NULL_HANDLE for the
    // lifetime of the app if the device doesn't support sceneSampleCount's sample count — density
    // pass (RecordDensityPass) then draws directly into densityImage instead.
    VkImage        densityMsaaImage{ VK_NULL_HANDLE };
    VkDeviceMemory densityMsaaMemory{ VK_NULL_HANDLE };
    VkImageView    densityMsaaView{ VK_NULL_HANDLE };
    VkImage        densityImage{ VK_NULL_HANDLE };
    VkDeviceMemory densityMemory{ VK_NULL_HANDLE };
    VkImageView    densityView{ VK_NULL_HANDLE };

    // RenderOffscreenFrame()'s render target — same format as the swapchain, sized to match it at
    // StartRecording() time (see recordWidth_/recordHeight_) and recreated only if that changes.
    VkImage        offscreenImage{ VK_NULL_HANDLE };
    VkDeviceMemory offscreenMemory{ VK_NULL_HANDLE };
    VkImageView    offscreenView{ VK_NULL_HANDLE };
    VkExtent2D     offscreenExtent{};
    // offscreenImage's density counterparts — same role/lifetime as densityMsaaImage/densityImage
    // above, just sized to/recreated alongside the offscreen target instead of the swapchain.
    VkImage        offscreenDensityMsaaImage{ VK_NULL_HANDLE };
    VkDeviceMemory offscreenDensityMsaaMemory{ VK_NULL_HANDLE };
    VkImageView    offscreenDensityMsaaView{ VK_NULL_HANDLE };
    VkImage        offscreenDensityImage{ VK_NULL_HANDLE };
    VkDeviceMemory offscreenDensityMemory{ VK_NULL_HANDLE };
    VkImageView    offscreenDensityView{ VK_NULL_HANDLE };
    // Whether offscreenImage has ever been rendered to — its very first barrier transitions from
    // UNDEFINED, every one after that from TRANSFER_SRC_OPTIMAL (where the previous frame's
    // capture copy left it).
    bool           offscreenEverRendered{ false };
    void EnsureOffscreenTarget(uint32_t w, uint32_t h);
    void DestroyOffscreenTarget();

    VkCommandPool   commandPool{ VK_NULL_HANDLE };
    VkCommandBuffer cb{ VK_NULL_HANDLE };

    VkSemaphore acquireSem{ VK_NULL_HANDLE };
    VkSemaphore renderSem{ VK_NULL_HANDLE };
    VkFence     fence{ VK_NULL_HANDLE };

    VkDescriptorSetLayout shapeDescSetLayout{ VK_NULL_HANDLE };
    VkDescriptorPool      descPool{ VK_NULL_HANDLE };
    VkDescriptorSet       circleDescSet{ VK_NULL_HANDLE };
    VkDescriptorSet       rectDescSet{ VK_NULL_HANDLE };

    // Composite pass: samples densityView/offscreenDensityView (a single combined-image-sampler
    // binding) and draws a fullscreen triangle that unpremultiplies the accumulated density into
    // final color — see kDensityFormat's comment. Two descriptor sets because the swapchain and
    // offscreen density images are sized independently and can be resolved to at different times
    // (see RenderOffscreenFrame's comment on why both paths exist); each is re-pointed at its
    // target's current VkImageView whenever that view is recreated (CreateSwapchain/
    // EnsureOffscreenTarget), since a descriptor holds a specific view handle, not a "current view"
    // indirection.
    VkSampler              densitySampler{ VK_NULL_HANDLE };
    VkDescriptorSetLayout  compositeDescSetLayout{ VK_NULL_HANDLE };
    VkDescriptorSet        swapchainCompositeDescSet{ VK_NULL_HANDLE };
    VkDescriptorSet        offscreenCompositeDescSet{ VK_NULL_HANDLE };
    VkPipelineLayout       compositePipelineLayout{ VK_NULL_HANDLE };
    VkPipeline             compositePipeline{ VK_NULL_HANDLE };
    void CreateCompositePipeline();
    // Writes `view` (must be in SHADER_READ_ONLY_OPTIMAL) into `set`'s binding 0.
    void UpdateCompositeDescSet(VkDescriptorSet set, VkImageView view);

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
    bool              captureFromSwapchain_{ true }; // see SetCaptureFromSwapchain

    // Hands pendingCaptureSlot_ (if any) to Recorder and clears it. Only valid to call once the
    // GPU copy into that slot is known to have finished (i.e. after a fence wait or
    // vkDeviceWaitIdle) — called both per-frame and from StopRecording(), so a stop never drops
    // the frame the most recent RenderFrame() call just captured.
    void FlushPendingCapture();

    void CreateSwapchain();
    void RecreateSwapchain();
    // Pass 1 of the scene draw, shared verbatim by RenderFrame() and RenderOffscreenFrame() — clears
    // `drawView` (transparent black) and draws only the current circles into it at `extent`, additively
    // accumulating density (see kDensityFormat's comment), then clears the circles vector. Doesn't
    // begin/end the command buffer or touch barriers; callers own those (including transitioning
    // `drawView`'s image to COLOR_ATTACHMENT_OPTIMAL beforehand).
    //
    // `resolveView` is VK_NULL_HANDLE when sceneSampleCount is 1 (no MSAA support): `drawView` is
    // then the final single-sample density target and this behaves like the pre-MSAA version — load
    // /clear/store it directly, no resolve. Otherwise `drawView` must be the matching MSAA target
    // (densityMsaaView/offscreenDensityMsaaView) and `resolveView` the single-sample density image
    // the multisampled result resolves into as part of this same pass; the MSAA image itself is left
    // DONT_CARE since nothing ever reads it back.
    void RecordDensityPass(VkImageView drawView, VkImageView resolveView, VkExtent2D extent);
    // Pass 2 of the scene draw — clears `finalView` to the scene background color, draws a fullscreen
    // triangle through `compositeSet` (must already be bound to the resolved density image the
    // matching RecordDensityPass call just wrote — see UpdateCompositeDescSet) to turn accumulated
    // density into real color, then draws the current rects on top and clears that vector. Callers
    // own barriers, same contract as RecordDensityPass.
    void RecordCompositePass(VkDescriptorSet compositeSet, VkImageView finalView, VkExtent2D extent);
    void CreateShapePipeline(const char* vertSpv, const char* fragSpv,
                             VkDescriptorSetLayout descSetLayout, VkFormat colorFormat,
                             VkSampleCountFlagBits sampleCount, const VkPipelineColorBlendAttachmentState& blendAtt,
                             VkPipelineLayout& outLayout, VkPipeline& outPipeline);
    // `preferred` flags are matched on top of HOST_VISIBLE where available (falling back to
    // plain HOST_VISIBLE|HOST_COHERENT otherwise); pass HOST_CACHED for a readback buffer the
    // CPU will read from — device memory that's HOST_VISIBLE|HOST_COHERENT but not cached is
    // typically an uncached PCIe-mapped region, and CPU reads from it can be an order of
    // magnitude slower than reads from normal (cached) system memory.
    void CreateSSBO(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags preferred,
                    VkBuffer& buf, VkDeviceMemory& mem, void*& mapped, bool* outCoherent = nullptr);

    // Shared image+memory+view allocation for any of this class's device-local color attachments
    // (offscreenImage, and now the MSAA/density targets below) — `usage` distinguishes the ones that
    // also need to be copy-read (TRANSFER_SRC) or sampled (SAMPLED) from the MSAA ones that only
    // ever get rendered into. `format` defaults to imageFormat (the swapchain/offscreen format);
    // the density targets pass kDensityFormat instead.
    void CreateColorImage(VkExtent2D extent, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
                          VkImage& img, VkDeviceMemory& mem, VkImageView& view, VkFormat format = VK_FORMAT_UNDEFINED);
    // Destroys and nulls out an image created by CreateColorImage — a no-op if `img` is already
    // VK_NULL_HANDLE. Callers own waiting for the GPU to be done with it first.
    void DestroyColorImage(VkImage& img, VkDeviceMemory& mem, VkImageView& view);
};
