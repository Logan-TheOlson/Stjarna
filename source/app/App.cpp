#include "App.h"

void App::Init(const char* title, int w, int h) {
    window.Init(title, w, h);
    vk.Init(window.Handle());
}

bool App::PollEvents()  { return window.PollEvents(); }
void App::RenderFrame() { vk.RenderFrame(); }

void App::AddCircle(float cx, float cy, float radius, Color color) { vk.AddCircle(cx, cy, radius, color); }

void App::Shutdown() {
    vk.Shutdown();
    window.Shutdown();
}
