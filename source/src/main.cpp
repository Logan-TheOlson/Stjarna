#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "engine/Engine.h"
#include "Config.h"

// ----------- Spatial partitioning
// Uniform grid, cell width = 2*SmoothingRadius, so every particle within SmoothingRadius of
// another is guaranteed to be found by checking just the 3x3 block of cells around it — no need
// to scan all particles like the old O(n^2) loops did.
struct GridCell { int x, y; bool operator==(const GridCell& o) const { return x == o.x && y == o.y; } };
struct GridCellHash {
    size_t operator()(const GridCell& c) const {
        return std::hash<int64_t>()((static_cast<int64_t>(c.x) << 32) ^ static_cast<uint32_t>(c.y));
    }
};

static std::unordered_map<GridCell, std::vector<int>, GridCellHash> grid;

static GridCell CellOf(const Vec2& pos, float cellSize) {
    return { static_cast<int>(std::floor(pos.x / cellSize)), static_cast<int>(std::floor(pos.y / cellSize)) };
}

// Rebuilt once per Update() call — cheap after warm-up since the map's bucket vectors are cleared
// in place (kept allocated) rather than the whole map being torn down and rebuilt every call.
static void BuildGrid() {
    for (auto& [cell, bucket] : grid) bucket.clear();
    const float cellSize = 2.f * Config::Particles::SmoothingRadius;
    for (int i = 0; i < static_cast<int>(objects.size()); i++)
        grid[CellOf(objects[i].pos, cellSize)].push_back(i);
}

// Invokes fn(Object&) for every particle sharing particle's cell or one of its 8 neighbors
// (including particle itself — callers already handle self-exclusion where it matters).
template <typename Fn>
static void ForEachNeighbor(const Object& particle, Fn&& fn) {
    const float cellSize = 2.f * Config::Particles::SmoothingRadius;
    const GridCell base = CellOf(particle.pos, cellSize);
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            auto it = grid.find({ base.x + dx, base.y + dy });
            if (it == grid.end()) continue;
            for (int idx : it->second)
                fn(objects[idx]);
        }
    }
}

// ----------- Kernels
static float DensityKernel (float radius, float dst) // Poly6-style kernel
{
    float val = std::max(0.f, radius*radius - dst*dst);
    constexpr float pi = 3.14159265358979323846f;
    float normalization = 315.f / (64.f * pi * std::pow(radius, 9));
    return normalization * val * val * val;
}

static float PressureKernelGradient (float radius, float dst) // 'Spiky' Kernel Gradient: to prevent lack of pressure at dst=0 in DensityKernel
{
    // (radius - dst)^3 is the pressure kernel, but we only need the gradient for use in pressure accumulation

    if (dst >= radius) return 0.f; // removes particles outside smoothing radius
    constexpr float pi = 3.14159265358979323846f;
    float normalization = -45.f / (pi * std::pow(radius, 6));
    return normalization * (radius - dst) * (radius - dst);
}

// --------- Calculations
static void CalculatePressure (Object& particle) // Calculate pressure
{
    float frac = Config::Particles::Stiffness * Config::Particles::TargetDensity * (1.f / Config::Particles::Exponent);
    float parenth = std::pow(particle.density/Config::Particles::TargetDensity, Config::Particles::Exponent) - 1;
    // Clamp negative pressure (density below target density, e.g. near a free surface) to zero —
    // otherwise the pressure force below turns attractive instead of just going slack.
    particle.pressure = std::max(0.f, frac * parenth);
}

static void CalculateDensity (Object& particle) // Calculates local density at a particle
{
    particle.density = 0.f;

    ForEachNeighbor(particle, [&](Object& b) {
        float dist = distance(b.pos, particle.pos);

        // A particle's self-term (dist==0) is the kernel's largest single contribution — TargetDensity
        // was calibrated assuming it's included, so skipping it left density at roughly half of target.
        if (dist > Config::Particles::SmoothingRadius) return;

        particle.density += DensityKernel(Config::Particles::SmoothingRadius, dist);
    });
}

static void CalculatePressureForce (Object& particle)
{
    constexpr float MinDensity = Config::Particles::TargetDensity * 0.01f;

    const float pDensity = std::max(particle.density, MinDensity);
    Vec2 forceVec(0.f, 0.f);

    ForEachNeighbor(particle, [&](Object& b) {
        if (&b == &particle) return;

        Vec2 difference = particle.pos - b.pos;
        float dist = magnitude(difference);
        if (dist == 0.f ) return;
        Vec2 dir = difference / dist;

        float grad = PressureKernelGradient(Config::Particles::SmoothingRadius, dist);
        float bDensity = std::max(b.density, MinDensity);
        float coefficient = particle.pressure / (pDensity * pDensity) + b.pressure / (bDensity * bDensity);

        Vec2 velDiff = particle.vel - b.vel;
        float approach = dot(velDiff, difference);
        if (approach < 0.f) {
            const float h  = Config::Particles::SmoothingRadius;
            const float mu = h * approach / (dist * dist + 0.01f * h * h);
            const float avgDensity = (pDensity + bDensity) * 0.5f;
            const float soundSpeed = std::sqrt(Config::Particles::Stiffness);
            coefficient += -Config::Particles::Viscosity * soundSpeed * mu / avgDensity;
        }

        forceVec -= dir * (grad * coefficient);
    });

    particle.acc += forceVec;
}

// ------------- Simulation
void Init() {
    // Sets Up sim
    constexpr int   gridCount = 20;
    constexpr float radius    = Config::Defaults::CircleRadius;
    constexpr float spacing   = radius * 3.0f;
    constexpr float startX    = -((gridCount - 1) * spacing / 2.0f);
    constexpr float startY    = -((gridCount - 1) * spacing / 2.0f);
    for (int x = 0; x < gridCount; x++)
        for (int y = 0; y < gridCount; y++)
            CreateObject(startX + static_cast<float>(x) * spacing,
                         startY + static_cast<float>(y) * spacing,
                         Renderable{ .color={0.2f, 0.6f, 1.0f, 1.0f}, .shader=Shader::Circle, .geometry=Circle{radius} });
}

void Update(float) {
    BuildGrid();

    for (auto& b : objects) // For every particle...
    {
        CalculateDensity(b);
        CalculatePressure(b);
    }
    for (auto& b : objects)
    {
        CalculatePressureForce(b);
        b.acc.y -= Config::Physics::Gravity;
    }
}
