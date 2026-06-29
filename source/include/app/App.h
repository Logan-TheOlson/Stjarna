#pragma once
#include "Window.h"
#include "VulkanContext.h"
#include "../Profiler.h"

class App {
public:
    void Init(const char* title, int w, int h);
    bool PollEvents();
    void RenderFrame();
    void Shutdown();
    void AddCircle(float cx, float cy, float radius, Color color);
    void AddRectangle(float cx, float cy, float halfW, float halfH, Color color);
    bool TakeF1Toggle()                             { return window.TakeF1Toggle(); }
    void SetProfilerOpen(bool open)                 { vk.SetProfilerOpen(open); }
    void SetProfilerStats(const Profiler::Stats& s) { vk.SetProfilerStats(s); }
    void SetObjectCount(int n)                      { vk.SetObjectCount(n); }
    int   Width()      const;
    int   Height()     const;
    float HalfWidth()  const;
    float HalfHeight() const;

private:
    Window        window;
    VulkanContext vk;
};
