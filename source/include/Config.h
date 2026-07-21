#pragma once

struct Color { float r, g, b, a; };

namespace Config {
    constexpr const char* WindowTitle  = "Stjarna";
    constexpr int         WindowWidth  = 1920;
    constexpr int         WindowHeight = 1080;

    namespace Defaults {
        constexpr float CircleRadius = 10.0f;
        constexpr Color CircleColor  = { 1.0f, 0.5f, 0.1f, 1.0f };
    }

    namespace Physics {
        constexpr float Gravity     = 2500.0f;  // pixels/s^2
        constexpr float Restitution = 0.85f;
        constexpr float Friction    = 0.3f;
        constexpr int   Substeps    = 8;

        constexpr float SmoothingRadius = 250.f;
    }
}
