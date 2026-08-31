#include <cmath>
#include <complex>

#include "engine/Engine.h"
#include "Config.h"

#include <iostream>

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

static float SmoothingKernel (float radius, float dst)
{
    float val = std::max(0.f, radius*radius - dst*dst);
    return val * val * val;
}

float CalculatePressure (float particleDensity)
{
    float frac = Config::Particles::Stiffness * Config::Particles::RestDensity * (1/Config::Particles::Exponent);
    float parenth = std::pow(particleDensity/Config::Particles::RestDensity, Config::Particles::Exponent) -1;
    return frac * parenth;
}

void CalculateDensity (Object& particle)
{
    particle.density = 0.f;

    for (auto& b : objects)
    {
        float dist = distance(b.pos, particle.pos);

        if (dist == 0.f ){continue;}
        if (dist > Config::Particles::SmoothingRadius){continue;}

        particle.density += SmoothingKernel(Config::Particles::SmoothingRadius, dist);
    }
}


void Init() {
    constexpr float radius  = Config::Defaults::CircleRadius;
    constexpr float spacing = radius * 5.0f;
    constexpr float startX  = -(4 * spacing / 2.0f);
    constexpr float startY  = -(4 * spacing / 2.0f);
    for (int x = 0; x < 5; x++)
        for (int y = 0; y < 5; y++)
            CreateObject(startX + static_cast<float>(x) * spacing,
                         startY + static_cast<float>(y) * spacing,
                         Renderable{ .color={0.2f, 0.6f, 1.0f, 1.0f}, .shader=Shader::Circle, .geometry=Circle{radius} });


}

void Update(float) {
    ResolveBoundaries();

    for (auto& b : objects)
    {
        b.vel.y += -0.25f;
    }

    CalculateDensity(objects[0]);
    std::cout << objects[0].density << std::endl;
}
