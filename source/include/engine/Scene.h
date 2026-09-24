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

// BarnesHut implements the 3D Newtonian (1/r^2) law restricted to the simulation plane (see
// Quadtree.h's QuadNode comment); Genuine2D instead implements the softened log-potential (1/r)
// law a real 2D universe's Poisson equation gives (see Gravity.cpp's EvaluateForce2D) —
// physically correct for a simulation that's actually 2D, and the default for that reason.
enum class GravityMethod { BarnesHut, Genuine2D };

// Self-gravity between particles, layered on top of ScenePhysics::force. Off by default — most
// scenes (channel flow, open tank) want only the constant external force, not an N-body pull.
// method/parameters are per-scene so, e.g., a stellar-formation preset can run at its own tuned
// G/softening while another scene keeps gravity off entirely.
struct SceneGravity {
    bool          enabled          = false;
    GravityMethod method           = GravityMethod::Genuine2D;
    float         g                = 20.f;
    float         particleMass     = 1.f;
    float         softening        = 7.9f; // Plummer softening length, avoids the 1/r^2 singularity as r -> 0
    float         macTheta         = 0.5f; // Barnes-Hut node-acceptance criterion
    int           maxLeafParticles = 8;
    bool          validate         = false; // periodically compares the active method against brute-force on a small sample
};

// Which pressure-from-density law main.cpp's CalculatePressure applies. Per-scene, since a scene
// meant to match a known analytical equilibrium (e.g. Self-Gravity vs. Lane-Emden) wants the pure
// polytrope, while the fluid scenes want WCSPH's stiffness against a specific rest density.
enum class EosModel {
    // Becker-Teschner weakly-compressible SPH: pressure = stiffness*targetDensity/exponent *
    // ((density/targetDensity)^exponent - 1), clamped to >=0. Only approximates a pure polytrope
    // once density >> targetDensity; the -1 offset matters close to targetDensity.
    WCSPH,
    // Pure polytrope: pressure = stiffness * density^(1 + 1/polytropicIndex), no targetDensity
    // offset — the equation of state a Lane-Emden equilibrium is derived against.
    Polytropic,
};

struct SceneParticles {
    float circleRadius = 3.0f;
    Color circleColor  = { 0.2f, 0.6f, 1.0f, 1.0f };

    // Calibrated to smoothingRadius=15 and a grid spacing of circleRadius*3=9px — recompute
    // (lattice-sum the Poly6 kernel at the new spacing/radius) if either changes.
    float smoothingRadius    = 15.0f;
    float targetDensity      = 6.48e-4f;
    float stiffness          = 12000.f;

    EosModel eos             = EosModel::WCSPH;
    int      exponent        = 7;         // WCSPH only
    float    polytropicIndex = 1.f / 6.f; // Polytropic only; gamma = 1 + 1/polytropicIndex

    float viscosity          = 0.5f;
    // Monaghan artificial viscosity quadratic-term coefficient (beta) — see main.cpp's
    // CalculatePressureForce for why this must stay well under 1.
    float viscosityQuadratic = 0.05f;
};

// How Init() lays out the starting particles: a gridCountX x gridCountY block (or, if `circular`,
// a circle inscribed in a block of that size) of spacing-separated particles, centered vertically
// and horizontally offset by offsetXFrac * the boundary's half-width.
struct SceneSpawn {
    int   gridCountX  = 100;
    int   gridCountY  = 100;
    float offsetXFrac = 0.0f;
    bool  circular     = false;
};

struct Scene {
    const char*     name;
    const char*     description;
    SceneBoundary   boundary;
    ScenePhysics    physics;
    SceneGravity    gravity;
    SceneParticles  particles;
    SceneSpawn      spawn;

    // Real-Time Limit: hand-tuned grid size this scene stays smooth/stable at in real time on
    // the developer's own machine — not measured by the sim itself, just shown for reference in
    // the menu (Engine.cpp's DrawMenu). Independent of spawn.gridCountX/Y (the scene's own
    // default count), since BuildScene() only overrides the latter.
    int             realTimeLimitX = 0;
    int             realTimeLimitY = 0;
};

extern const std::vector<Scene> ScenePresets;

// Rebuilds ScenePresets[presetIndex] with its particle grid resized to gridCountX x gridCountY
// instead of the preset's own default count — re-deriving whatever calibration depends on that
// count (e.g. MakeSelfGravity's mass/softening/stiffness scaling), not just overwriting
// SceneSpawn's fields on a copy, which would leave a resolution-dependent scene tuned for the
// wrong count. gridCountX/Y are clamped to >= 1.
Scene BuildScene(int presetIndex, int gridCountX, int gridCountY);

// The scene currently governing physics/spawn/boundary. Mutable — set via LoadScene(), not
// assigned directly, so derived SPH kernel constants (main.cpp's RecomputeSphConstants) stay in
// sync with it.
extern Scene ActiveScene;

// Recomputes cached kernel constants (main.cpp) from `scene`'s particle settings. Does not spawn
// particles — call Init() after, once `objects` has been reset.
void LoadScene(const Scene& scene);
