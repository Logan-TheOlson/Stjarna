#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "engine/Engine.h"
#include "Config.h"
#include "util/ThreadPool.h"

// ----------- Spatial partitioning
// Uniform grid (cell = 2*SmoothingRadius) as a CSR-sorted flat hash rather than
// unordered_map<Cell, vector<int>> — no per-cell heap allocations, and cellStart[h]..cellStart[h+1]
// is a contiguous slice instead of a pointer-chased bucket walk.
//
// The same sort reorders a "hot" copy of pos/vel/density/pressure/force by cell, kept separate
// from Object/Renderable. The density/force loops then only touch ~40 bytes/particle and read
// mostly-contiguous memory per cell, instead of scattering through `objects` in creation order.
//
// The table is unbounded, so cells can collide in the hash; cellX/cellY reject false positives so
// a collision only wastes a candidate rather than double-counting one.
namespace {
    constexpr float Radius   = Config::Particles::SmoothingRadius;
    constexpr float RadiusSq = Radius * Radius;
    constexpr float CellSize = 2.f * Radius;

    std::vector<int32_t>  rawCellX, rawCellY;   // per real particle, scratch for BuildGrid
    std::vector<uint32_t> rawHash;

    std::vector<Vec2>    hotPos, hotVel, hotForce;
    std::vector<float>   hotDensity, hotPressure;
    std::vector<int32_t> hotToReal;    // hot slot -> index into `objects`
    std::vector<int32_t> cellX, cellY; // per hot slot, integer cell coords
    std::vector<int32_t> cellStart;    // [tableSize+1] CSR offsets into the hot arrays
    std::vector<int32_t> cursor;       // scratch write cursor per bucket during the scatter pass
    uint32_t tableSize = 0;
}

static int32_t CellCoord(float p) { return static_cast<int32_t>(std::floor(p / CellSize)); }

// Teschner et al.'s spatial-hash primes; unsigned so the multiply can't hit signed overflow UB.
static uint32_t HashCell(int32_t x, int32_t y) {
    return (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u);
}

static uint32_t NextPow2(uint32_t v) {
    v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

// Rebuilt once per Update(). All arrays are namespace-scope and reused across calls, so a stable
// particle count allocates nothing after warm-up.
static void BuildGrid() {
    const int32_t n = static_cast<int32_t>(objects.size());
    tableSize = std::max<uint32_t>(16u, NextPow2(static_cast<uint32_t>(n) * 2u));

    rawCellX.resize(n);
    rawCellY.resize(n);
    rawHash.resize(n);
    cellStart.assign(tableSize + 1, 0);

    for (int32_t i = 0; i < n; i++) {
        const int32_t cx = CellCoord(objects[i].pos.x);
        const int32_t cy = CellCoord(objects[i].pos.y);
        rawCellX[i] = cx;
        rawCellY[i] = cy;
        const uint32_t h = HashCell(cx, cy) & (tableSize - 1);
        rawHash[i] = h;
        cellStart[h + 1]++;
    }
    for (uint32_t h = 0; h < tableSize; h++) cellStart[h + 1] += cellStart[h]; // counts -> start offsets

    hotToReal.resize(n);
    cellX.resize(n);
    cellY.resize(n);
    hotPos.resize(n);
    hotVel.resize(n);
    hotDensity.resize(n);
    hotPressure.resize(n);
    hotForce.resize(n);

    cursor.assign(cellStart.begin(), cellStart.end() - 1);
    for (int32_t i = 0; i < n; i++) {
        const int32_t k = cursor[rawHash[i]]++;
        hotToReal[k] = i;
        cellX[k] = rawCellX[i];
        cellY[k] = rawCellY[i];
        hotPos[k] = objects[i].pos;
        hotVel[k] = objects[i].vel;
    }
}

// Invokes fn(int32_t hotSlot) for every hot-array slot sharing pos's cell or one of its 8
// neighbors. `pos` need not belong to a real particle (see ForEachGhost); a caller wanting to
// exclude itself compares hot-slot indices.
template <typename Fn>
static void ForEachNeighbor(const Vec2& pos, Fn&& fn) {
    const int32_t cx = CellCoord(pos.x);
    const int32_t cy = CellCoord(pos.y);
    for (int32_t dx = -1; dx <= 1; dx++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            const int32_t nx = cx + dx, ny = cy + dy;
            const uint32_t h = HashCell(nx, ny) & (tableSize - 1);
            for (int32_t k = cellStart[h]; k < cellStart[h + 1]; k++) {
                if (cellX[k] != nx || cellY[k] != ny) continue; // hash-collision false positive
                fn(k);
            }
        }
    }
}

// Mirror-image ("method of images") boundary particles. SPH density near a wall is under-counted
// (missing kernel mass from beyond the wall), which zero-clamps pressure there and lets particles
// pile up against it. Reflecting nearby real particles across the wall and folding them into the
// density/pressure sums like normal neighbors restores that missing mass directly.
//
// Position mirrors exactly; velocity mirrors with the wall-normal component negated (free-slip);
// density/pressure carry over unchanged (the field is assumed locally symmetric across the wall).
// Carries the hot-slot index rather than density/pressure directly — those are still being written
// by other threads during the (parallel) density phase, so reading them here would race.
struct Ghost { Vec2 pos; Vec2 vel; int32_t src; };

struct WallMirror { bool active = false; float wall = 0.f; };

// Which wall (if either) `coord` is within Radius of, on one axis. `half` is the true container
// edge (ScreenHalfWidth/Height), not the particle-radius-inset range particle centers clamp to.
static WallMirror NearestWall(float coord, float half) {
    if (coord + half < Radius) return { true, -half };
    if (half - coord < Radius) return { true, half };
    return {};
}

// Invokes fn(const Ghost&) for every real particle whose mirror image — across whichever wall(s)
// `pos` is within Radius of, including the diagonal double-mirror in a corner — falls within
// Radius of `pos`. No-op away from all walls.
template <typename Fn>
static void ForEachGhost(const Vec2& pos, Fn&& fn) {
    const WallMirror wx = NearestWall(pos.x, ScreenHalfWidth());
    const WallMirror wy = NearestWall(pos.y, ScreenHalfHeight());

    auto pass = [&](bool mirrorX, bool mirrorY) {
        Vec2 query = pos;
        if (mirrorX) query.x = 2.f * wx.wall - pos.x;
        if (mirrorY) query.y = 2.f * wy.wall - pos.y;

        ForEachNeighbor(query, [&](int32_t j) {
            Vec2 gp = hotPos[j], gv = hotVel[j];
            if (mirrorX) { gp.x = 2.f * wx.wall - hotPos[j].x; gv.x = -hotVel[j].x; }
            if (mirrorY) { gp.y = 2.f * wy.wall - hotPos[j].y; gv.y = -hotVel[j].y; }
            fn(Ghost{ gp, gv, j });
        });
    };

    if (wx.active)               pass(true,  false);
    if (wy.active)               pass(false, true);
    if (wx.active && wy.active)  pass(true,  true);
}

// ----------- Kernels
namespace {
    constexpr float Pi        = 3.14159265358979323846f;
    // radius^6 / radius^9, expanded by hand since std::pow isn't constexpr — turns a std::pow
    // call on every particle pair into a plain multiply against a value computed once.
    constexpr float Radius6   = RadiusSq * RadiusSq * RadiusSq;
    constexpr float Radius9   = Radius6 * RadiusSq * Radius;
    constexpr float DensityNorm      = 315.f / (64.f * Pi * Radius9);
    constexpr float PressureGradNorm = -45.f / (Pi * Radius6);
    // Stiffness is constexpr but std::sqrt isn't; computed once instead of per neighbor pair.
    const     float SoundSpeed = std::sqrt(Config::Particles::Stiffness);
}

// Poly6-style kernel. Takes squared distance directly — callers already have it, and the kernel
// only ever needs dst^2, so there's no reason to sqrt just to square it back.
static float DensityKernel (float distSq)
{
    float val = RadiusSq - distSq;
    return DensityNorm * val * val * val;
}

// 'Spiky' Kernel Gradient: to prevent lack of pressure at dst=0 in DensityKernel.
// Caller guarantees 0 < dst < Radius.
static float PressureKernelGradient (float dst)
{
    float val = Radius - dst;
    return PressureGradNorm * val * val;
}

// Repeated-squaring integer power — avoids std::pow's exp/log path for a fixed small exponent.
static float IntPow (float base, int exp)
{
    float result = 1.f;
    while (exp) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

// --------- Calculations
// Operate on hot-array slots; Object is untouched until Update() scatters the results back.

static void CalculatePressure (int32_t k) // Calculate pressure
{
    float frac = Config::Particles::Stiffness * Config::Particles::TargetDensity * (1.f / Config::Particles::Exponent);
    float parenth = IntPow(hotDensity[k] / Config::Particles::TargetDensity, Config::Particles::Exponent) - 1;
    // Clamp negative pressure so that it is never attractive
    hotPressure[k] = std::max(0.f, frac * parenth);
}

static void CalculateDensity (int32_t k) // Calculates local density at a particle
{
    const Vec2 pos = hotPos[k];
    float density = 0.f;

    auto accumulate = [&](const Vec2& bPos) {
        Vec2 diff = pos - bPos;
        float distSq = dot(diff, diff);
        if (distSq > RadiusSq) return; // includes the dist==0 self-term; TargetDensity assumes it

        density += DensityKernel(distSq);
    };

    ForEachNeighbor(pos, [&](int32_t j) { accumulate(hotPos[j]); });
    ForEachGhost(pos, [&](const Ghost& g) { accumulate(g.pos); }); // no self-exclusion, see Ghost comment

    hotDensity[k] = density;
}

static void CalculatePressureForce (int32_t k)
{
    constexpr float MinDensity = Config::Particles::TargetDensity * 0.01f;

    const Vec2  pos       = hotPos[k];
    const Vec2  vel       = hotVel[k];
    const float pDensity  = std::max(hotDensity[k], MinDensity);
    const float pPressure = hotPressure[k];
    Vec2 forceVec(0.f, 0.f);

    // Shared pairwise force, used for both real neighbors and ghost mirror images below.
    auto accumulate = [&](const Vec2& bPos, const Vec2& bVel, float bDensityRaw, float bPressure) {
        Vec2 difference = pos - bPos;
        float distSq = dot(difference, difference);
        // Reject on squared distance before paying for a sqrt — the 3x3 cell block covers a much
        // larger area than the smoothing radius circle, so most candidates fail this check.
        if (distSq >= RadiusSq || distSq == 0.f) return;

        float dist = std::sqrt(distSq);
        Vec2 dir = difference / dist;

        float grad = PressureKernelGradient(dist);
        float bDensity = std::max(bDensityRaw, MinDensity);
        float coefficient = pPressure / (pDensity * pDensity) + bPressure / (bDensity * bDensity);

        Vec2 velDiff = vel - bVel;
        float approach = dot(velDiff, difference);
        if (approach < 0.f) {
            const float mu = Radius * approach / (distSq + 0.01f * RadiusSq);
            const float avgDensity = (pDensity + bDensity) * 0.5f;
            coefficient += -Config::Particles::Viscosity * SoundSpeed * mu / avgDensity;
        }

        forceVec -= dir * (grad * coefficient);
    };

    ForEachNeighbor(pos, [&](int32_t j) {
        if (j == k) return;
        accumulate(hotPos[j], hotVel[j], hotDensity[j], hotPressure[j]);
    });

    // No self-exclusion: a particle right at the wall does feel a force from its own mirror image
    ForEachGhost(pos, [&](const Ghost& g) {
        accumulate(g.pos, g.vel, hotDensity[g.src], hotPressure[g.src]);
    });

    hotForce[k] = forceVec;
}

// ------------- Simulation
void Init() {
    constexpr float radius     = Config::Defaults::CircleRadius;
    constexpr float spacing    = radius * 3.0f;
    constexpr int   gridCountX = 200, gridCountY = 125; // 25,000
    constexpr float startX     = -((gridCountX - 1) * spacing / 2.0f);
    constexpr float startY     = -((gridCountY - 1) * spacing / 2.0f);
    for (int x = 0; x < gridCountX; x++)
        for (int y = 0; y < gridCountY; y++)
            CreateObject(startX + static_cast<float>(x) * spacing,
                         startY + static_cast<float>(y) * spacing,
                         Renderable{ .color={0.2f, 0.6f, 1.0f, 1.0f}, .shader=Shader::Circle, .geometry=Circle{radius} });
}

void Update(float) {
    BuildGrid();

    const int32_t n = static_cast<int32_t>(hotPos.size());

    // Each k writes only its own hot slot, so these are datarace-free; ParallelFor blocks until a
    // call's chunks all finish, so density/pressure are fully settled before the force pass reads them.
    ParallelFor(n, [](int32_t begin, int32_t end) {
        for (int32_t k = begin; k < end; k++) { CalculateDensity(k); CalculatePressure(k); }
    });
    ParallelFor(n, [](int32_t begin, int32_t end) {
        for (int32_t k = begin; k < end; k++) CalculatePressureForce(k);
    });

    // Scatter hot results back onto the real objects.
    for (int32_t k = 0; k < n; k++) {
        Object& o = objects[hotToReal[k]];
        o.density   = hotDensity[k];
        o.pressure  = hotPressure[k];
        o.acc      += hotForce[k];
        o.acc.y    -= Config::Physics::Gravity;
    }
}
