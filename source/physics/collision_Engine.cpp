#include "collision_Engine.h"
#include "../Circle.h"
#include "../Config.h"
#include <cmath>

namespace CollisionEngine {
    void ResolveBoundary(Circle& circle, float halfW, float halfH) {
        float right  =  halfW - circle.radius;
        float left   = -halfW + circle.radius;
        float top    =  halfH - circle.radius;
        float bottom = -halfH + circle.radius;

        if (circle.x > right)  { circle.x = right;  circle.vx = -circle.vx * Config::Physics::HorizontalDamping; }
        if (circle.x < left)   { circle.x = left;   circle.vx = -circle.vx * Config::Physics::HorizontalDamping; }
        if (circle.y > top)    { circle.y = top;     circle.vy = -circle.vy * Config::Physics::VerticalDamping; }
        if (circle.y < bottom) {
            circle.y = bottom;
            circle.vy = -circle.vy * Config::Physics::VerticalDamping;
        }

    }
}
