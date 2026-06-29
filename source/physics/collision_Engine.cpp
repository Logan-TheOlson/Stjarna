#include "collision_Engine.h"
#include "../Object.h"
#include "../Config.h"
#include <cassert>

void CollisionEngine::ResolveBoundary(Object& obj) const {
    assert(halfW > 0.0f && halfH > 0.0f);
    const float r = obj.shape.radius;
    if (obj.x >  halfW - r) { obj.x =  halfW - r; obj.vx = -obj.vx * Config::Physics::Restitution; }
    if (obj.x < -halfW + r) { obj.x = -halfW + r; obj.vx = -obj.vx * Config::Physics::Restitution; }
    if (obj.y >  halfH - r) { obj.y =  halfH - r; obj.vy = -obj.vy * Config::Physics::Restitution; }
    if (obj.y < -halfH + r) { obj.y = -halfH + r; obj.vy = -obj.vy * Config::Physics::Restitution; }
}
