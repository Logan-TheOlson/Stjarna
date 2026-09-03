#include <algorithm>
#include <cmath>

#include "engine/Engine.h"
#include "Config.h"

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

    for (auto& b : objects)
    {
        float dist = distance(b.pos, particle.pos);

        // A particle's self-term (dist==0) is the kernel's largest single contribution — TargetDensity
        // was calibrated assuming it's included, so skipping it left density at roughly half of target.
        if (dist > Config::Particles::SmoothingRadius){continue;}

        particle.density += DensityKernel(Config::Particles::SmoothingRadius, dist);
    }
}

static void CalculatePressureForce (Object& particle)
{
    // Floor density before it's used as a divisor — a particle with few/no neighbors
    // (edge, corner, momentarily isolated) can have density near zero without being
    // exactly zero, which would otherwise blow up the 1/density terms below.
    constexpr float MinDensity = Config::Particles::TargetDensity * 0.01f;

    const float pDensity = std::max(particle.density, MinDensity);
    Vec2 forceVec(0.f, 0.f);

    for (auto& b : objects)
    {
        if (&b == &particle) continue;

       Vec2 difference = particle.pos - b.pos;
        float dist = magnitude(difference);
        if (dist == 0.f ) continue;
        Vec2 dir = difference / dist;

        float grad = PressureKernelGradient(Config::Particles::SmoothingRadius, dist);
        float bDensity = std::max(b.density, MinDensity);

        // Standard SPH momentum equation: F_i = -sum_j (P_i/rho_i^2 + P_j/rho_j^2) * gradW_ij.
        // The old (P_i+P_j)/(2*rho_i*rho_j) weighting was momentum-conserving (antisymmetric by
        // construction) but NOT energy-conserving — it only equals this form when rho_i==rho_j,
        // and diverges whenever densities differ, which is most of the time in a real fluid. This
        // specific per-particle 1/rho^2 weighting is what's actually derivable from the SPH energy
        // functional; confirmed via a live kinetic-energy readout that climbed every collision
        // even after ruling out force staleness as the cause.
        float coefficient = particle.pressure / (pDensity * pDensity) + b.pressure / (bDensity * bDensity);
        forceVec -= dir * (grad * coefficient);
    }

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
    for (auto& b : objects) // For every particle...
    {
        CalculateDensity(b);
        CalculatePressure(b);
    }
    for (auto& b : objects)
    {
        CalculatePressureForce(b);
    }
}
