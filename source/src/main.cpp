#include "engine/Engine.h"
#include "Config.h"

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

static void ResolveBoundaries() {
    const float hw = ScreenHalfWidth(), hh = ScreenHalfHeight();
    for (auto& b : objects) {
        const float r = b.Radius();
        if (b.x - r < -hw) { b.x = -hw + r; if (b.vx < 0.0f) b.vx *= -Config::Physics::Restitution; }
        if (b.x + r >  hw) { b.x =  hw - r; if (b.vx > 0.0f) b.vx *= -Config::Physics::Restitution; }
        if (b.y - r < -hh) { b.y = -hh + r; if (b.vy < 0.0f) b.vy *= -Config::Physics::Restitution; }
        if (b.y + r >  hh) { b.y =  hh - r; if (b.vy > 0.0f) b.vy *= -Config::Physics::Restitution; }
    }
}

void Update(float) {

    
    ResolveBoundaries();
}

