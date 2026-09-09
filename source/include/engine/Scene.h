#pragma once
#include "Config.h"
#include "util/Vector.h"
#include <vector>

// Simulation container half-extents, as a fraction of the window's half-extents (1.0 = walls at
// the window edge). Kept relative rather than absolute so presets don't hardcode window pixels.
struct SceneBoundary {
    float widthFrac  = 1.0f;
    float heightFrac = 1.0f;
    // If set, the left/right walls don't bounce particles — a particle crossing one teleports to
    // the other side instead (position only; velocity untouched), simulating an infinite domain
    // for a channel-flow test. main.cpp mirrors real particles across the seam too (see
    // ForEachPeriodicX) so density/pressure near the seam are computed as if particles from the
    // opposite edge really are nearby, matching what an infinite domain would give.
    bool  periodicX  = false;
};

struct ScenePhysics {
    // Constant acceleration applied to every particle every frame (pixels/s^2) — gravity is just
    // the common case of this pointing down; a horizontal force drives channel-flow tests instead.
    Vec2  force          = Vec2(0.f, -600.f);
    float restitution   = 0.85f;
    // Fraction of tangential velocity removed on wall contact when noSlipWalls is enabled.
    float friction       = 0.3f;
    bool  noSlipWalls    = true;
    int   substeps       = 12;
    int   forceInterval  = 1;
};

struct SceneParticles {
    float circleRadius = 3.0f;
    Color circleColor  = { 0.2f, 0.6f, 1.0f, 1.0f };

    // Calibrated to smoothingRadius=15 and a grid spacing of circleRadius*3=9px — recompute
    // (lattice-sum the Poly6 kernel at the new spacing/radius) if either changes.
    float smoothingRadius    = 15.0f;
    float targetDensity      = 6.48e-4f;
    float stiffness          = 12000.f;
    int   exponent           = 7;
    float viscosity          = 0.5f;
    // Monaghan artificial viscosity quadratic-term coefficient (beta) — see main.cpp's
    // CalculatePressureForce for why this must stay well under 1.
    float viscosityQuadratic = 0.05f;
};

// How Init() lays out the starting particles: a gridCountX x gridCountY block (or, if `circular`,
// a circle inscribed in a block of that size) of spacing-separated particles, centered vertically
// and horizontally offset by offsetXFrac * the boundary's half-width.
struct SceneSpawn {
    int   gridCountX  = 150;
    int   gridCountY  = 125;
    float offsetXFrac = 0.0f;
    bool  circular     = false;
};

struct Scene {
    const char*     name;
    const char*     description;
    SceneBoundary   boundary;
    ScenePhysics    physics;
    SceneParticles  particles;
    SceneSpawn      spawn;
};

extern const std::vector<Scene> ScenePresets;

// The scene currently governing physics/spawn/boundary. Mutable — set via LoadScene(), not
// assigned directly, so derived SPH kernel constants (main.cpp's RecomputeSphConstants) stay in
// sync with it.
extern Scene ActiveScene;

// Recomputes cached kernel constants (main.cpp) from `scene`'s particle settings. Does not spawn
// particles — call Init() after, once `objects` has been reset.
void LoadScene(const Scene& scene);
