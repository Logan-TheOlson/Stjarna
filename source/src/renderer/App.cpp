#include "renderer/App.h"

void App::Init(const char* title, int w, int h) {
    window.Init(title, w, h);
    vk.Init(window.Handle());
    vk.InitImGui(window.Handle());
}

bool App::PollEvents()  { return window.PollEvents(); }
void App::RenderFrame() { vk.RenderFrame(); }

void App::AddCircle(float cx, float cy, float radius, Color color) { vk.AddCircle(cx, cy, radius, color); }
int   App::Width()      const { return vk.Width(); }
int   App::Height()     const { return vk.Height(); }
float App::HalfWidth()  const { return vk.HalfWidth(); }
float App::HalfHeight() const { return vk.HalfHeight(); }

void App::Shutdown() {
    vk.Shutdown();
    window.Shutdown();
}
