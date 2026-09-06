#pragma once
#include <functional>
#include <string>
#include "Window.h"
#include "VulkanContext.h"
#include "util/Profiler.h"

class App {
public:
    void Init(const char* title, int w, int h);
    bool PollEvents();
    void RenderFrame(float dt);
    void Shutdown();
    void AddCircle(float cx, float cy, float radius, Color color);
    bool TakeF1Toggle()                             { return window.TakeF1Toggle(); }
    void SetProfilerOpen(bool open)                 { vk.SetProfilerOpen(open); }
    void SetProfilerStats(const Profiler::Stats& s) { vk.SetProfilerStats(s); }
    void SetObjectCount(int n)                      { vk.SetObjectCount(n); }
    void SetKineticEnergy(float ke)                 { vk.SetKineticEnergy(ke); }
    void SetUICallback(std::function<void()> cb)    { vk.SetUICallback(std::move(cb)); }
    int   Width()      const;
    int   Height()     const;
    float HalfWidth()  const;
    float HalfHeight() const;

    // Starts (or restarts) recording the composited frame to <recordingsDir>/<title>.mp4 via
    // ffmpeg. lengthSeconds <= 0 means "until StopRecording() is called".
    bool  StartRecording(const std::string& title, int fps, float lengthSeconds);
    void  StopRecording()                { vk.StopRecording(); }
    bool  IsRecording()            const { return vk.IsRecording(); }
    float RecordedSeconds()        const { return vk.RecordedSeconds(); }

private:
    Window        window;
    VulkanContext vk;
};
