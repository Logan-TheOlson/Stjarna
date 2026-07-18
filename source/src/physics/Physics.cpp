#include "physics/Physics.h"
#include "Object.h"
#include "Config.h"

void Physics::ApplyForce(Object& obj, float fx, float fy) {
    obj.ax += fx;
    obj.ay += fy;
}

void Physics::ResolveBoundaries(std::span<Object> objects, float hw, float hh) {
    for (auto& b : objects) {
        const float r = b.Radius();
        if (b.x - r < -hw) { b.x = -hw + r; if (b.vx < 0.0f) b.vx *= -Config::Physics::Restitution; }
        if (b.x + r >  hw) { b.x =  hw - r; if (b.vx > 0.0f) b.vx *= -Config::Physics::Restitution; }
        if (b.y - r < -hh) { b.y = -hh + r; if (b.vy < 0.0f) b.vy *= -Config::Physics::Restitution; }
        if (b.y + r >  hh) { b.y =  hh - r; if (b.vy > 0.0f) b.vy *= -Config::Physics::Restitution; }
    }
}
