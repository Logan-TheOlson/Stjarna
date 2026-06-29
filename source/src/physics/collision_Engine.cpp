#include "physics/collision_Engine.h"
#include "Object.h"
#include "Config.h"
#include <cassert>

void CollisionEngine::ResolveBoundary(Object& obj) const {
    assert(halfW > 0.0f && halfH > 0.0f);
    std::visit([&](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, Circle>) {
            const float r = s.radius;
            if (obj.x >  halfW - r) { obj.x =  halfW - r; obj.vx = -obj.vx * Config::Physics::Restitution; }
            if (obj.x < -halfW + r) { obj.x = -halfW + r; obj.vx = -obj.vx * Config::Physics::Restitution; }
            if (obj.y >  halfH - r) { obj.y =  halfH - r; obj.vy = -obj.vy * Config::Physics::Restitution; }
            if (obj.y < -halfH + r) { obj.y = -halfH + r; obj.vy = -obj.vy * Config::Physics::Restitution; }
        } else if constexpr (std::is_same_v<T, Rectangle>) {
            if (obj.x >  halfW - s.halfW) { obj.x =  halfW - s.halfW; obj.vx = -obj.vx * Config::Physics::Restitution; }
            if (obj.x < -halfW + s.halfW) { obj.x = -halfW + s.halfW; obj.vx = -obj.vx * Config::Physics::Restitution; }
            if (obj.y >  halfH - s.halfH) { obj.y =  halfH - s.halfH; obj.vy = -obj.vy * Config::Physics::Restitution; }
            if (obj.y < -halfH + s.halfH) { obj.y = -halfH + s.halfH; obj.vy = -obj.vy * Config::Physics::Restitution; }
        }
    }, obj.shape);
}
