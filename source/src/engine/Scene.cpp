#include "engine/Scene.h"

// Defined in main.cpp — recomputes the SPH kernel constants (DensityNorm, PressureGradNorm, ...)
// that are derived from SceneParticles and cached for the hot loop rather than recomputed per pair.
void RecomputeSphConstants();

namespace {
    Scene MakeOpenTank() {
        return Scene{
            .name = "Open Tank",
            .description = "Full-window box; a centered block of fluid falls and settles.",
        };
    }

    Scene MakeDamBreak() {
        Scene s = MakeOpenTank();
        s.name        = "Dam Break";
        s.description = "A column of fluid held near the left wall, released into an empty tank.";
        s.spawn.gridCountX  = 50;
        s.spawn.offsetXFrac = -0.6f;
        return s;
    }

    Scene MakeDroplet() {
        Scene s = MakeOpenTank();
        s.name        = "Droplet";
        s.description = "A circular blob of fluid dropped into the middle of an empty tank.";
        s.spawn.gridCountX = s.spawn.gridCountY = 126; // ~pi/4 fill factor keeps particle count near the other presets'
        s.spawn.circular    = true;
        return s;
    }

    Scene MakeNarrowTank() {
        Scene s = MakeOpenTank();
        s.name        = "Narrow Tank";
        s.description = "Half-width container — same fluid, walls close enough to matter.";
        s.boundary.widthFrac = 0.4f;
        return s;
    }

    Scene MakeSyrup() {
        Scene s = MakeOpenTank();
        s.name        = "Syrup";
        s.description = "Low gravity, heavy viscosity — slow, sluggish flow.";
        s.physics.gravity            = 250.f;
        s.particles.viscosity          = 2.5f;
        s.particles.viscosityQuadratic = 0.2f;
        s.particles.circleColor        = { 0.85f, 0.55f, 0.15f, 1.0f };
        return s;
    }

    Scene MakeZeroG() {
        Scene s = MakeOpenTank();
        s.name        = "Zero Gravity";
        s.description = "No gravity — fluid just jostles around under its own pressure.";
        s.physics.gravity     = 0.f;
        s.particles.circleColor = { 0.7f, 0.3f, 0.9f, 1.0f };
        return s;
    }
}

const std::vector<Scene> ScenePresets = {
    MakeOpenTank(), MakeDamBreak(), MakeDroplet(), MakeNarrowTank(), MakeSyrup(), MakeZeroG(),
};

Scene ActiveScene = ScenePresets[0];

void LoadScene(const Scene& scene) {
    ActiveScene = scene;
    RecomputeSphConstants();
}
