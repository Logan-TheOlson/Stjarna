#pragma once
#include "Window.h"
#include "VulkanContext.h"

class App {
public:
    void Init(const char* title, int w, int h);
    bool PollEvents();
    void RenderFrame();
    void Shutdown();
    void AddCircle(float cx, float cy, float radius, Color color);

private:
    Window        window;
    VulkanContext vk;
};
