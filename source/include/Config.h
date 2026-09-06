#pragma once

struct Color { float r, g, b, a; };

// Window setup only — everything tunable per-run (boundaries, spawn layout, physics/particle
// constants) now lives in Scene.h/Scene.cpp as runtime scene presets.
namespace Config {
    constexpr const char* WindowTitle  = "Stjarna";
    constexpr int         WindowWidth  = 2560;
    constexpr int         WindowHeight = 1440;
}
