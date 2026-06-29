#include "ImpulseSolver.h"
#include "../Object.h"
#include "../Config.h"
#include "collision_Engine.h"
#include "SpatialGrid.h"
#include <cmath>

void ImpulseSolver::Solve(std::span<Object> objects, SpatialGrid& grid, float hw, float hh, float dt) {
    CollisionEngine engine{ hw, hh };
    for (auto& obj : objects) {
        obj.vy -= Config::Physics::Gravity * dt;
        obj.vx *= Config::Physics::Damping;
        obj.vy *= Config::Physics::Damping;
        if (std::abs(obj.vx) < Config::Physics::SleepThreshold) obj.vx = 0.0f;
        if (std::abs(obj.vy) < Config::Physics::SleepThreshold) obj.vy = 0.0f;
        obj.x  += obj.vx * dt;
        obj.y  += obj.vy * dt;
        engine.ResolveBoundary(obj);
    }

    for (int step = 0; step < Config::Physics::Substeps; step++) {
        grid.Clear();
        for (int i = 0; i < static_cast<int>(objects.size()); i++)
            grid.Insert(i, objects[i].x, objects[i].y);
    }
}
