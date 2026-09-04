#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "engine/Engine.h"
#include "Config.h"
#include "physics/FMM.h"
#include "physics/Gravity.h"
#include "physics/Quadtree.h"
#include "util/ThreadPool.h"

namespace {
    constexpr float Radius   = Config::Particles::SmoothingRadius;
    constexpr float RadiusSq = Radius * Radius;
    constexpr float CellSize = 2.f * Radius;

    std::vector<int32_t>  rawCellX, rawCellY;
    std::vector<uint32_t> rawHash;

    std::vector<Vec2>    hotPos, hotVel, hotForce, hotGravAcc;
    std::vector<float>   hotDensity, hotPressure;
    std::vector<int32_t> hotToReal;
    std::vector<int32_t> cellX, cellY;
    std::vector<int32_t> cellStart;
    std::vector<int32_t> cursor;
    uint32_t tableSize = 0;
}

static int32_t CellCoord(float p) { return static_cast<int32_t>(std::floor(p / CellSize)); }

static uint32_t HashCell(int32_t x, int32_t y) {
    return (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u);
}

static uint32_t NextPow2(uint32_t v) {
    v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

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
    for (uint32_t h = 0; h < tableSize; h++) cellStart[h + 1] += cellStart[h];

    hotToReal.resize(n);
    cellX.resize(n);
    cellY.resize(n);
    hotPos.resize(n);
    hotVel.resize(n);
    hotDensity.resize(n);
    hotPressure.resize(n);
    hotForce.resize(n);
    hotGravAcc.resize(n);

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

template <typename Fn>
static void ForEachNeighbor(const Vec2& pos, Fn&& fn) {
    const int32_t cx = CellCoord(pos.x);
    const int32_t cy = CellCoord(pos.y);
    for (int32_t dx = -1; dx <= 1; dx++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            const int32_t nx = cx + dx, ny = cy + dy;
            const uint32_t h = HashCell(nx, ny) & (tableSize - 1);
            for (int32_t k = cellStart[h]; k < cellStart[h + 1]; k++) {
                if (cellX[k] != nx || cellY[k] != ny) continue;
                fn(k);
            }
        }
    }
}

namespace {
    constexpr float Pi        = 3.14159265358979323846f;
    constexpr float Radius6   = RadiusSq * RadiusSq * RadiusSq;
    constexpr float Radius9   = Radius6 * RadiusSq * Radius;
    constexpr float DensityNorm      = 315.f / (64.f * Pi * Radius9);
    constexpr float PressureGradNorm = -45.f / (Pi * Radius6);
    constexpr float ExpectedCentralDensity = 2.f * 3000.f / (Pi * 300.f * 300.f);
    const     float SoundSpeed = std::sqrt(2.f * Config::Particles::PolytropicK * ExpectedCentralDensity);
}

static float DensityKernel (float distSq)
{
    float val = RadiusSq - distSq;
    return DensityNorm * val * val * val;
}

static float PressureKernelGradient (float dst)
{
    float val = Radius - dst;
    return PressureGradNorm * val * val;
}

static Color DensityToColor(float density) {
    const float t = std::clamp(density / ExpectedCentralDensity, 0.f, 1.f);
    const float g = 0.75f * (1.f - t);
    return Color{ 1.f, g, 0.f, 1.f };
}

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

static void CalculatePressure (int32_t k)
{
    hotPressure[k] = Config::Particles::PolytropicK * IntPow(hotDensity[k], Config::Particles::Gamma);
}

static void CalculateDensity (int32_t k)
{
    const Vec2 pos = hotPos[k];
    float density = 0.f;

    auto accumulate = [&](const Vec2& bPos) {
        Vec2 diff = pos - bPos;
        float distSq = dot(diff, diff);
        if (distSq > RadiusSq) return;

        density += DensityKernel(distSq);
    };

    ForEachNeighbor(pos, [&](int32_t j) { accumulate(hotPos[j]); });

    hotDensity[k] = density;
}

static void CalculatePressureForce (int32_t k)
{
    constexpr float MinDensity = DensityNorm * Radius6;

    const Vec2  pos       = hotPos[k];
    const Vec2  vel       = hotVel[k];
    const float pDensity  = std::max(hotDensity[k], MinDensity);
    const float pPressure = hotPressure[k];
    Vec2 forceVec(0.f, 0.f);

    auto accumulate = [&](const Vec2& bPos, const Vec2& bVel, float bDensityRaw, float bPressure) {
        Vec2 difference = pos - bPos;
        float distSq = dot(difference, difference);
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

    hotForce[k] = forceVec;
}

static void InitKeplerTest() {
    constexpr float radius = Config::Defaults::CircleRadius;
    constexpr float d      = Config::Debug::KeplerSeparation * 0.5f;

    // Circular mutual orbit: m*v^2/d == G*m^2/r^2 with r = 2d, so v = sqrt(G*m / (2*r)).
    const float v = std::sqrt(Config::Physics::G * Config::Physics::ParticleMass / (2.f * Config::Debug::KeplerSeparation));

    Object& a = CreateObject(-d, 0.f, Renderable{ .color={0.2f, 0.6f, 1.0f, 1.0f}, .shader=Shader::Circle, .geometry=Circle{radius} });
    a.vel = Vec2(0.f, v);

    Object& b = CreateObject(d, 0.f, Renderable{ .color={1.0f, 0.6f, 0.2f, 1.0f}, .shader=Shader::Circle, .geometry=Circle{radius} });
    b.vel = Vec2(0.f, -v);
}

void Init() {
    if constexpr (Config::Debug::KeplerTestEnabled) {
        InitKeplerTest();
        return;
    }

    constexpr float radius = Config::Defaults::CircleRadius;
    constexpr int   n      = 10000;

    std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> spawnX(-ScreenHalfWidth(),  ScreenHalfWidth());
    std::uniform_real_distribution<float> spawnY(-ScreenHalfHeight(), ScreenHalfHeight());

    for (int i = 0; i < n; i++) {
        CreateObject(spawnX(rng), spawnY(rng),
                     Renderable{ .color={0.2f, 0.6f, 1.0f, 1.0f}, .shader=Shader::Circle, .geometry=Circle{radius} });
    }
}

void Update(float) {
    BuildGrid();

    const int32_t n = static_cast<int32_t>(hotPos.size());

    ParallelFor(n, [](int32_t begin, int32_t end) {
        for (int32_t k = begin; k < end; k++) { CalculateDensity(k); CalculatePressure(k); }
    });
    ParallelFor(n, [](int32_t begin, int32_t end) {
        for (int32_t k = begin; k < end; k++) CalculatePressureForce(k);
    });

    if constexpr (Config::Physics::UseFMM) {
        Gravity::BuildQuadtree(hotPos, n);
        Gravity::ComputeMultipoles(hotPos);
        Gravity::EvaluateFMM(hotPos, n, hotGravAcc);
        if constexpr (Config::Physics::ValidateFMM) {
            Gravity::ValidateFMMAccuracy(hotPos, n, 500);
        }
    } else {
        Gravity::BuildTree(hotPos, n);
        ParallelFor(n, [](int32_t begin, int32_t end) {
            for (int32_t k = begin; k < end; k++) hotGravAcc[k] = Gravity::EvaluateForce(k, hotPos);
        });
        if constexpr (Config::Physics::ValidateGravity) {
            Gravity::ValidateAccuracy(hotPos, n, 500);
        }
    }

    for (int32_t k = 0; k < n; k++) {
        Object& o = objects[hotToReal[k]];
        o.density   = hotDensity[k];
        o.pressure  = hotPressure[k];
        o.acc      += hotForce[k];
        o.acc      += hotGravAcc[k];
        o.renderable.color = DensityToColor(hotDensity[k]);
    }
}
