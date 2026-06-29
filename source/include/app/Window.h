#pragma once
#include <SDL3/SDL.h>

class Window {
public:
    void Init(const char* title, int w, int h);
    bool PollEvents();
    void Shutdown();

    SDL_Window* Handle() const { return window; }
    bool TakeF1Toggle();   // returns true once per F1 press

private:
    SDL_Window* window{ nullptr };
    bool        f1Toggled_{ false };
};
