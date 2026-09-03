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
        constexpr float Gravity     = 600.0f;  // pixels/s^2
        constexpr float Restitution = 0.85f;
        constexpr float Friction    = 0.3f;
        constexpr int   Substeps    = 8;
        constexpr int   ForceInterval = 1;
        static_assert(Substeps % ForceInterval == 0, "ForceInterval must divide Substeps evenly");
    }

    namespace Particles
    {
        constexpr float SmoothingRadius = 50.f;
        // TargetDensity calibrated to the normalized Poly6 kernel at SmoothingRadius=50
        // with the current Init() grid spacing (30px) — recompute if either changes.
        constexpr float TargetDensity = 1.75e-5f;
        constexpr float Stiffness = 12000.f;
        constexpr int Exponent = 7;
        constexpr float Viscosity = 0.5f;
    }
}
