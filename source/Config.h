#pragma once

struct Vec2  { float x, y; };
struct Color { float r, g, b, a; };

namespace Config {
    constexpr const char* WindowTitle  = "Stjarna";
    constexpr int         WindowWidth  = 1920;
    constexpr int         WindowHeight = 1080;

    namespace Defaults {
        constexpr Vec2  CirclePos    = { 0.0f, 0.0f };
        constexpr float CircleRadius = 10.0f;
        constexpr Color CircleColor  = { 1.0f, 0.5f, 0.1f, 1.0f };
    }

    namespace Physics {
        constexpr float HorizontalDamping = 0.85f;
        constexpr float VerticalDamping   = 0.85f;
    }
}
