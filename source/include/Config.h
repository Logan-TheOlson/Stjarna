#pragma once

struct Color { float r, g, b, a; };

namespace Config {
    constexpr const char* WindowTitle  = "Stjarna";
    constexpr int         WindowWidth  = 2560;
    constexpr int         WindowHeight = 1440;

    namespace Defaults {
        // Scaled down from 2.5f by sqrt(10000/25000) when particle count went to
        // 25k, to keep roughly the same visual packing density on screen.
        constexpr float CircleRadius = 1.6f;
        constexpr Color CircleColor  = { 1.0f, 0.5f, 0.1f, 1.0f };
    }

    namespace Particles
    {
        // Scaled down from 12.5f by sqrt(10000/25000) alongside the particle-count
        // increase to 25k, so the average neighbor count per particle (and thus the
        // SPH density/pressure estimate) stays roughly the same as at 10k — without
        // this, going to 25k particles with the same radius would inflate density
        // estimates ~2.5x and pressure ~6x (Gamma=2), destabilizing the fluid.
        constexpr float SmoothingRadius = 7.9f;
        constexpr float PolytropicK = 750000.f;
        constexpr int   Gamma = 2;
        constexpr float Viscosity = 1.5f;
    }

    namespace Physics {
        constexpr int   Substeps      = 8;
        // Keep at 1: tried 2 and 4 (reusing a substep's force across multiple
        // integrate steps) at 25k particles to cut Update()'s cost — SPH pressure
        // here is too stiff (steep polytropic response) to tolerate it. Measured
        // real energy drift/injection, not just imperceptible staleness: at 2,
        // KE stopped plateauing and climbed ~28% over ~4.5s and kept rising; at 4,
        // KE blew up to ~10^13 within seconds. Gravity alone tolerates staleness
        // fine (that's a much gentler force); the coupled SPH+gravity Update()
        // does not. A real fix would decouple gravity's update cadence from SPH's
        // rather than sharing one interval — not attempted here.
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
        // EvaluateFMM parallelizes its dual-tree traversal across the 10 top-level
        // (child_i, child_j) pairs among the root's 4 children via ParallelFor,
        // each on a private local-expansion/accel scratch buffer merged afterward
        // (contributions are purely additive, so this is exact, not approximate).
        // Even so, at this particle count FMM is SLOWER than Barnes-Hut in wall-clock
        // terms (measured, release build, 10k particles): BH ~43-50ms/frame (~20-24fps)
        // vs FMM ~170-190ms/frame (~5-6fps). This is expected, not a bug: FMM's O(N) vs
        // BH's O(N log N) only wins once N is large enough to outweigh FMM's much bigger
        // per-interaction constant here (a full order-3 M2L is a double-precision 28-term
        // D-table + a naive O(p^4) convolution, versus BH's ~20-30-flop monopole+quadrupole
        // per node) — that crossover is typically far above 10k particles for a naive
        // (non-FFT/rotation-accelerated) Cartesian M2L implementation like this one.
        // Leave false; flip true only to exercise/benchmark/validate the FMM path itself.
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
