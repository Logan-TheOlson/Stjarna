#include "engine/Scene.h"

// Defined in main.cpp — recomputes the SPH kernel constants (DensityNorm, PressureGradNorm, ...)
// that are derived from SceneParticles and cached for the hot loop rather than recomputed per pair.
void RecomputeSphConstants();

namespace {
    Scene MakeOpenTank() {
        Scene s{
            .name = "Open Tank",
            .description = "Full-window box; a centered block of fluid falls and settles.",
        };
        // ForEachGhost's no-slip ghosts (full velocity reversal, not just the wall-normal
        // component) were added for Poiseuille's wall drag and are much stronger than the old
        // occasional friction-on-contact — this scene predates and wasn't tuned against that
        // effect, so opt back into the original free-slip walls explicitly rather than silently
        // inheriting ScenePhysics::noSlipWalls' default.
        s.physics.noSlipWalls = false;
        return s;
    }

    // Classic force-driven planar Poiseuille flow: no gravity, just a uniform horizontal force
    // pushing fluid left-to-right through a narrow no-slip channel (solid top/bottom walls). The
    // left/right boundary is periodic — a particle exiting the right edge reappears at the left,
    // and main.cpp's ForEachPeriodicX mirrors real particles across that seam for density/pressure
    // too — so the channel behaves as if it were infinitely long, the standard setup for this
    // test. Viscous drag against the walls (vs. the free interior) should develop into the classic
    // parabolic velocity profile: fastest at the channel's center, near-zero at the walls.
    Scene MakePoiseuille() {
        Scene s = MakeOpenTank();
        s.name        = "Poiseuille Flow";
        s.description = "Periodic horizontal channel; a uniform force (no gravity) drives fluid "
                         "left-to-right, developing the classic parabolic velocity profile against "
                         "the no-slip walls.";
        s.boundary.heightFrac = 0.2f;
        s.boundary.periodicX  = true;
        s.spawn.gridCountX    = 280;
        s.spawn.gridCountY    = 22;
        // Tuned so the plug settles into equilibrium quickly and at a speed still slow enough to
        // watch: higher viscosity damps the initial transient faster (and pulls the eventual
        // plug speed down on its own), lower force pulls the terminal speed down further on top
        // of that — both push the same direction, so they compound rather than fight each other.
        s.physics.force         = Vec2(100.f, 0.f);
        s.physics.noSlipWalls   = true; // the wall drag this whole scene exists to demonstrate
        s.particles.viscosity   = 4.0f;
        s.particles.circleColor = { 0.2f, 0.9f, 0.8f, 1.0f };
        return s;
    }
}

const std::vector<Scene> ScenePresets = {
    MakeOpenTank(), MakePoiseuille(),
};

Scene ActiveScene = ScenePresets[0];

void LoadScene(const Scene& scene) {
    ActiveScene = scene;
    RecomputeSphConstants();
}
