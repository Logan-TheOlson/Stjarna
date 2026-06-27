#include "Window.h"
#include <iostream>

void Window::Init(const char* title, int w, int h) {
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow(title, w, h, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n"; exit(1); }
}

bool Window::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT) return false;
    return true;
}

void Window::Shutdown() {
    SDL_DestroyWindow(window);
    SDL_Quit();
}
