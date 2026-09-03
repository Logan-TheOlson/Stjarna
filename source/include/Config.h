#pragma once

struct Color { float r, g, b, a; };

namespace Config {
    constexpr const char* WindowTitle  = "Stjarna";
    constexpr int         WindowWidth  = 2560;
    constexpr int         WindowHeight = 1440;

    namespace Defaults {
        constexpr float CircleRadius = 2.5f;
        constexpr Color CircleColor  = { 1.0f, 0.5f, 0.1f, 1.0f };
    }

    namespace Physics {
        constexpr float Omega = 1.5f;
        constexpr int   Substeps      = 8;
        constexpr int   ForceInterval = 1;
        static_assert(Substeps % ForceInterval == 0, "ForceInterval must divide Substeps evenly");
    }

    namespace Particles
    {
        constexpr float SmoothingRadius = 12.5f;
        constexpr float PolytropicK = 750000.f;
        constexpr int   Gamma = 2;
        constexpr float Viscosity = 1.5f;
    }
}
