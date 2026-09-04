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

    namespace Particles
    {
        constexpr float SmoothingRadius = 12.5f;
        constexpr float PolytropicK = 750000.f;
        constexpr int   Gamma = 2;
        constexpr float Viscosity = 1.5f;
    }

    namespace Physics {
        constexpr int   Substeps      = 8;
        constexpr int   ForceInterval = 1;
        static_assert(Substeps % ForceInterval == 0, "ForceInterval must divide Substeps evenly");

        // Self-gravity (Barnes-Hut treecode)
        constexpr float G                = 20.f;   // tuned so G*N*ParticleMass/r^2 gives a plausible collapse timescale at typical cluster radius
        constexpr float ParticleMass     = 1.f;     // uniform, matches implicit SPH unit-mass assumption
        constexpr float GravSoftening    = Particles::SmoothingRadius; // ~mean interparticle spacing, avoids singularity as r -> 0
        constexpr float MACTheta         = 0.5f;    // Barnes-Hut node-acceptance criterion
        constexpr int   MaxLeafParticles = 8;
        constexpr bool  ValidateGravity  = false;   // periodically compares treecode force against brute-force on a small sample

        // Self-gravity (FMM, order 3 Cartesian multipole/local expansions)
        // NOTE: FMM's dual-tree traversal (EvaluateFMM) is single-threaded (unlike
        // EvaluateForce below, which runs per-particle across all CPU threads via
        // ParallelFor) — measured ~550ms/call at 10k particles, i.e. ~4.4s/frame
        // at Substeps=8, vs. Barnes-Hut's real-time performance. Leave false until
        // the traversal is parallelized; toggle true only to exercise/validate FMM.
        constexpr bool  UseFMM           = false;   // true: Gravity::EvaluateFMM (O(N)); false: Gravity::EvaluateForce Barnes-Hut (O(N log N))
        constexpr bool  ValidateFMM      = false;   // periodically compares FMM force against brute-force on a small sample
        // Needs to be notably smaller than MACTheta: the dual-tree MAC compares
        // *summed* bounding-circle radii (halfSize*sqrt2, to correctly bound a
        // square cell's corner) against center-to-center distance, which is a
        // more permissive criterion than Barnes-Hut's single-node-vs-point MAC,
        // and p=3's truncation error grows quickly once separation is marginal.
        // 0.5 measured ~38% max/1.6% mean force error at 10k particles; 0.3
        // measured ~3% max/0.3% mean (comparable to Barnes-Hut's own ~2.3%/0.15%).
        constexpr float FMMTheta         = 0.3f;
    }

    namespace Debug {
        constexpr bool  KeplerTestEnabled         = false; // spawns a 2-body circular-orbit scene instead of the normal cloud
        constexpr float KeplerSeparation           = 50.f; // >> SmoothingRadius (12.5), so SPH force is ~0 and this isolates gravity
        constexpr int   EnergyMonitorMaxParticles = 500;   // above this, skip the O(n^2) potential-energy calc

        // Note: at the production G=20/ParticleMass=1 scale (tuned for the ~10k-particle
        // cluster's aggregate force), a 2-body orbit at this separation has a period of
        // hundreds of simulated seconds — too slow to watch. Verified instead with G
        // temporarily bumped to 5000 (self-consistent, since both the orbital-velocity
        // formula and the applied force read Config::Physics::G): energy stayed flat to
        // ~0.004% and the orbit traced a closed loop rather than spiraling in/out.
    }
}
