#pragma once

struct Color { float r, g, b, a; };

namespace Config {
    constexpr const char* WindowTitle  = "Stjarna";
    constexpr int         WindowWidth  = 2560;
    constexpr int         WindowHeight = 1440;

    namespace Defaults {
        constexpr float CircleRadius = 3.0f;
        constexpr Color CircleColor  = { 1.0f, 0.5f, 0.1f, 1.0f };
    }

    namespace Physics {
        constexpr float Gravity     = 600.0f;  // pixels/s^2
        constexpr float Restitution = 0.85f;
        // Fraction of tangential velocity removed on wall contact when NoSlipWalls is enabled.
        constexpr float Friction    = 0.3f;
        constexpr bool  NoSlipWalls = true;
        constexpr int   Substeps    = 12;
        constexpr int   ForceInterval = 1;
        static_assert(Substeps % ForceInterval == 0, "ForceInterval must divide Substeps evenly");
    }

    namespace Particles
    {
        constexpr float SmoothingRadius = 15.0f;
        // Calibrated to SmoothingRadius=15 and Init()'s grid spacing (radius*3 = 9px) — recompute
        // (lattice-sum the Poly6 kernel at the new spacing/radius) if either changes. Derived from
        // the prior calibration (SmoothingRadius=12.5, spacing=7.5, TargetDensity=1.12e-3) by
        // scaling it by the ratio of analytical lattice sums at the two radii, since the
        // analytical sum alone doesn't reproduce 1.12e-3 exactly (that value came from measuring
        // the settled sim, not a pure lattice sum) but the ratio between two sums should still
        // hold across a uniform radius/spacing rescale.
        constexpr float TargetDensity = 6.48e-4f;
        constexpr float Stiffness = 12000.f;
        constexpr int Exponent = 7;
        constexpr float Viscosity = 0.5f;
        // Monaghan artificial viscosity quadratic-term coefficient (beta) — suppresses
        // interpenetration at high approach speeds, on top of the linear term's shear damping.
        // mu is velocity-scaled and this term grows as mu^2, while typical impact speeds here
        // (free-fall over the window height) already exceed SoundSpeed (sqrt(Stiffness)) —
        // so beta must stay well under 1 or it swamps the linear term and blows up under the
        // explicit substep integration. Keep small and raise cautiously.
        constexpr float ViscosityQuadratic = 0.05f;
    }
}
