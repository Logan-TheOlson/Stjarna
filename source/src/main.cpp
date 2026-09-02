#include <algorithm>
#include <cmath>

#include "engine/Engine.h"
#include "Config.h"

// ----------- Boundary limiting
static void resolveAxis(float& p, float& v, float half, float r) {
    if (p - r < -half) { p = -half + r; if (v < 0.0f) v *= -Config::Physics::Restitution; }
    if (p + r >  half) { p =  half - r; if (v > 0.0f) v *= -Config::Physics::Restitution; }
}

static void ResolveBoundaries() {
    const float hw = ScreenHalfWidth(), hh = ScreenHalfHeight();
    for (auto& b : objects) {
        const float r = b.Radius();
        resolveAxis(b.pos.x, b.vel.x, hw, r);
        resolveAxis(b.pos.y, b.vel.y, hh, r);
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

    if (dst >= radius) return 0.f; // removes particles outside smoothing radius TODO: Spatial Partitioning
    constexpr float pi = 3.14159265358979323846f;
    float normalization = -45.f / (pi * std::pow(radius, 6));
    return normalization * (radius - dst) * (radius - dst);
}

// --------- Calculations
void CalculatePressure (Object& particle) // Calculate pressure
{
    float frac = Config::Particles::Stiffness * Config::Particles::RestDensity * (1.f / Config::Particles::Exponent);
    float parenth = std::pow(particle.density/Config::Particles::RestDensity, Config::Particles::Exponent) - 1;
    particle.pressure = frac * parenth;
}

void CalculateDensity (Object& particle) // Calculates local density at a particle
{
    particle.density = 0.f;

    for (auto& b : objects)
    {
        float dist = distance(b.pos, particle.pos);

        if (dist == 0.f ){continue;}
        if (dist > Config::Particles::SmoothingRadius){continue;}

        particle.density += DensityKernel(Config::Particles::SmoothingRadius, dist);
    }
}

void CalculatePressureForce (Object& particle)
{
    // Floor density before it's used as a divisor — a particle with few/no neighbors
    // (edge, corner, momentarily isolated) can have density near zero without being
    // exactly zero, which would otherwise blow up the 1/density terms below.
    constexpr float MinDensity = Config::Particles::RestDensity * 0.01f;

    Vec2 forceVec(0.f, 0.f);

    for (auto& b : objects)
    {
        if (&b == &particle) continue;

       Vec2 difference = particle.pos - b.pos;
        float dist = magnitude(difference);
        if (dist == 0.f ) continue;
        Vec2 dir = difference / dist;

        float grad = PressureKernelGradient(Config::Particles::SmoothingRadius, dist);
        float sharedPressure = (particle.pressure + b.pressure) / 2.f;
        float bDensity = std::max(b.density, MinDensity);

        forceVec -= dir * (grad * sharedPressure / bDensity);
    }

    float pDensity = std::max(particle.density, MinDensity);
    particle.acc += forceVec / pDensity;
}

// ------------- Simulation
void Init() {
    // Sets Up sim
    constexpr float radius  = Config::Defaults::CircleRadius;
    constexpr float spacing = radius * 3.0f;
    constexpr float startX  = -(4 * spacing / 2.0f);
    constexpr float startY  = -(4 * spacing / 2.0f);
    for (int x = 0; x < 20; x++)
        for (int y = 0; y < 20; y++)
            CreateObject(startX + static_cast<float>(x) * spacing,
                         startY + static_cast<float>(y) * spacing,
                         Renderable{ .color={0.2f, 0.6f, 1.0f, 1.0f}, .shader=Shader::Circle, .geometry=Circle{radius} });
}

void Update(float) {
    // Window Boundaries
    ResolveBoundaries();

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
