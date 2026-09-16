#include "engine/Scene.h"
#include "physics/Gravity.h"
#include <cmath>

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

    // No external force at all — the only thing holding the block together (or pulling it apart)
    // is mutual N-body attraction between particles, via the genuine-2D treecode, plus SPH
    // pressure resisting the collapse. Default window boundaries (full-window box, free-slip
    // walls from MakeOpenTank), so the block has room to contract before it ever reaches a wall.
    Scene MakeSelfGravity() {
        Scene s = MakeOpenTank();
        s.name        = "Self-Gravity";
        s.description = "No external force; particles pull on each other via a genuine 2D (1/r) "
                         "self-gravity treecode while SPH pressure resists the collapse.";
        s.physics.force = Vec2(0.f, 0.f);

        s.gravity.enabled  = true;
        s.gravity.method   = GravityMethod::Genuine2D;

        // Circular spawn: a radially symmetric initial condition settles into a radially symmetric
        // equilibrium, which is what a Lane-Emden comparison (spherically/circularly symmetric by
        // construction) assumes — a rectangular block would settle off-center and skew any radial
        // density profile pulled from it.
        s.spawn.circular = true;

        // 50k particles — bumped up from this scene's original 10k baseline (OldCount below),
        // packed into the SAME physical disk size a different resolution of this scene would use
        // (same diskRadius, so the K/alpha calibration below targets the cloud this sim validated
        // against Lane-Emden) by scaling circleRadius by sqrt(oldCount/newCount): Init()'s
        // diskRadius = circleRadius*3*sqrt(count/pi), so this scale-factor machinery cancels any
        // count change relative to OldCount. smoothingRadius scales the same way to keep the same
        // smoothingRadius/spacing ratio (and hence a still-valid density kernel), which is also
        // what makes the particles render slightly smaller at this resolution — circleRadius
        // shrinks by the same sqrt(oldCount/newCount) factor. targetDensity does NOT scale like a
        // plain areal density (1/scale^2) here: this kernel's DensityNorm ~ 1/h^9 with a
        // (h^2-r^2)^3 falloff (a 3D Poly6 normalization, not the true 2D one), so a uniform spatial
        // rescale by `scale` moves every kernel evaluation by 1/scale^3 — see main.cpp's
        // DensityKernel/DensityNorm. At gridCountX*gridCountY == OldCount, scale == 1 and all of
        // this is a no-op — the knob to turn for a different resolution. (gridCountX/Y need not be
        // square-ish: circular spawn only ever uses their product, see Init()'s circular branch.)
        constexpr int OldCount = 100 * 100;
        s.spawn.gridCountX = 250;
        s.spawn.gridCountY = 200; // 250*200 == 50000
        const float scale = std::sqrt(static_cast<float>(OldCount)
                                     / static_cast<float>(s.spawn.gridCountX * s.spawn.gridCountY));
        s.particles.circleRadius    *= scale;
        s.particles.smoothingRadius *= scale;
        s.particles.targetDensity   /= (scale * scale * scale);

        // CalculateDensity's kernel sum has no mass weighting at all (see main.cpp — it's a plain
        // unit-per-particle count convolved with the kernel), so nothing above touches how much
        // *gravitating* mass this cloud has: that's particleCount*particleMass, entirely separate,
        // straight in Gravity's N-body sum. Left at particleMass=1, more particles at the same disk
        // size means more enclosed mass at every radius, so gravity would overpower the (correctly
        // recalibrated) pressure and the cloud would run away outward instead of sitting at
        // equilibrium. Scaling particleMass by scale^2 = oldCount/newCount holds enclosed-mass(r)
        // exactly fixed relative to OldCount (more particles x proportionally less mass each = the
        // same M(r) profile this scene's K was derived for) — a no-op at the current count.
        s.gravity.particleMass *= scale * scale;

        // substeps bumped from ScenePhysics's default (12, inherited from MakeOpenTank, and what
        // the original 10k baseline was validated stable at). A finer grid (smaller
        // smoothingRadius, stiffer EOS to match) pushes this scene's characteristic CFL number
        // (soundspeed/smoothingRadius, evaluated at targetDensity) up sharply — 50k particles (5x
        // the 10k baseline) was separately measured to need 96 substeps (8x). Revisit if
        // gridCountX/Y above change again.
        s.physics.substeps = 96;

        // Softening ~= smoothing radius, same "avoids the 1/r^2 singularity at ~mean interparticle
        // spacing" reasoning as SceneGravity's own default — kept in sync explicitly here (and
        // computed after the resolution scaling above, so it picks up the shrunk smoothingRadius).
        s.gravity.softening = s.particles.smoothingRadius;

        // Pure polytrope, not WCSPH's targetDensity-relative EOS: a Lane-Emden equilibrium is
        // derived against pressure = K*density^gamma with no offset, while WCSPH's -1 term only
        // becomes negligible once density >> targetDensity — it measurably skews the fit right
        // where the cloud's edge approaches targetDensity. n=1/6 keeps the same effective gamma=7
        // stiffness this scene used before switching off WCSPH.
        s.particles.eos             = EosModel::Polytropic;
        s.particles.polytropicIndex = 1.f / 6.f;

        // Stiffness calibrated so the spawned cloud starts (approximately) AT its own Lane-Emden
        // equilibrium, instead of a stiffness picked by feel and left to find whatever radius it
        // finds. Working backward from hydrostatic equilibrium + this sim's actual Genuine2D force
        // law (accel = 2*G*M/r, i.e. the Poisson source is 4*pi*G*density here, not the 2*pi*G a
        // "real 2D universe" comment elsewhere assumes — see Gravity.cpp's EvaluateForce2D) gives:
        //
        //   alpha^2 = (n+1) * K * rho_c^(1/n - 1) / (4*pi*G)
        //
        // where xi1 is the first zero of the n=1/6 Lane-Emden solution theta(xi), alpha=R/xi1, and
        // R is this scene's actual spawn disk radius (spacing*sqrt(count/pi), see Init()'s circular
        // branch). rho_c has to be targetDensity, NOT some independently-chosen "nicer" density
        // (e.g. picked to hit a round total mass): CalculateDensity's kernel sum has no notion of
        // physical mass/area, only whatever number this specific smoothingRadius+spacing combo
        // measures for a particle arrangement this dense — targetDensity is that number, calibrated
        // for exactly this spacing. Any other rho_c is a density the spawned particles will never
        // actually report, so K would be sized for a pressure the sim can't produce at t=0 — that
        // mismatch is what free-fall-collapsed an earlier version of this calibration into NaNs.
        // Solved offline in scratchpad/lane_emden_2d.py since n=1/6 has no closed form (only n=0, 1
        // do); recompute xi1 below if n changes, and K if n, G, targetDensity, spawn count, or
        // circleRadius/spacing change.
        constexpr float Xi1 = 2.060505463f; // first zero of theta(xi), n=1/6
        const float diskRadius = (s.particles.circleRadius * 3.f)
                               * std::sqrt(static_cast<float>(s.spawn.gridCountX * s.spawn.gridCountY) / 3.14159265f);
        const float alpha = diskRadius / Xi1;
        const float rhoC  = s.particles.targetDensity;
        const float n     = s.particles.polytropicIndex;
        s.particles.stiffness = 4.f * 3.14159265f * s.gravity.g * alpha * alpha
                              * std::pow(rhoC, 1.f - 1.f / n) / (n + 1.f);

        // This continuum formula checks out exactly (verified separately: the analytic pressure-vs-
        // gravity force balance it implies comes out to a 1.0000 ratio at this scene's alpha/rho_c,
        // for both this resolution and the 10k-particle one), and the treecode's own gravity is
        // independently accurate here (~0.2% mean error, checked via Gravity::ValidateAccuracy2D).
        // But run as derived, this scene still blows outward past the window wall within a fraction
        // of a second — so the mismatch is in how CalculatePressureForce's *discretized* pressure-
        // gradient kernel (a "Spiky" kernel with its own, different h-power normalization from the
        // density kernel above) represents that continuum force at this smoothingRadius, not in the
        // continuum physics or the gravity solver. Empirically, K/10 keeps the cloud oscillating
        // (amplitude ~680px) well inside the window instead of escaping — this factor is a measured
        // correction for that discretization gap, not a re-derived constant, so revisit it if
        // anything above (n, resolution, smoothingRadius) changes again.
        s.particles.stiffness *= 0.1f;
        return s;
    }
}

const std::vector<Scene> ScenePresets = {
    MakeOpenTank(), MakePoiseuille(), MakeSelfGravity(),
};

Scene ActiveScene = ScenePresets[0];

void LoadScene(const Scene& scene) {
    ActiveScene = scene;
    RecomputeSphConstants();
    Gravity::SetParams({
        .g                = scene.gravity.g,
        .particleMass     = scene.gravity.particleMass,
        .softening        = scene.gravity.softening,
        .macTheta         = scene.gravity.macTheta,
        .maxLeafParticles = scene.gravity.maxLeafParticles,
    });
}
