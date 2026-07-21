#include <cmath>

#include "engine/Engine.h"
#include "Config.h"

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

void Update(float) {
    ResolveBoundaries();
}

void Init() {
    constexpr float radius  = Config::Defaults::CircleRadius;
    constexpr float spacing = radius * 10.0f;
    constexpr float startX  = -(4 * spacing / 2.0f);
    constexpr float startY  = -(4 * spacing / 2.0f);
    for (int x = 0; x < 5; x++)
        for (int y = 0; y < 5; y++)
            CreateObject(startX + static_cast<float>(x) * spacing,
                         startY + static_cast<float>(y) * spacing,
                         Renderable{ .color={0.2f, 0.6f, 1.0f, 1.0f}, .shader=Shader::Circle, .geometry=Circle{radius} });
}