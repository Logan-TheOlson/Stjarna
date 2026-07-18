#include "renderer/Window.h"
#include <imgui_impl_sdl3.h>
#include <iostream>

void Window::Init(const char* title, int w, int h) {
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow(title, w, h, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; exit(1); }
}

bool Window::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) return false;
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F1)
            f1Toggled_ = true;
    }
    return true;
}

bool Window::TakeF1Toggle() {
    if (!f1Toggled_) return false;
    f1Toggled_ = false;
    return true;
}

void Window::Shutdown() {
    SDL_DestroyWindow(window);
    SDL_Quit();
}
