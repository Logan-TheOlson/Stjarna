#include "app/App.h"
#include "Object.h"
#include "physics/Physics.h"
#include "physics/SpatialGrid.h"
#include "Profiler.h"
#include "Config.h"
#include <chrono>
#include <cmath>
#include <vector>

App                 app;
std::vector<Object> objects;
SpatialGrid         grid;
Profiler            profiler;
bool                profilerOpen = false;

// O(1) removal — swaps with last element, invalidating index i
void removeObject(size_t i) {
    objects[i] = objects.back();
    objects.pop_back();
}

Object& createObject(float x, float y, const Circle& shape) {
    objects.push_back({ x, y, 0.0f, 0.0f, shape });
    return objects.back();
}

Object& createObject(float x, float y, const Rectangle& shape) {
    objects.push_back({ x, y, 0.0f, 0.0f, shape });
    return objects.back();
}

// Called once before the first frame — equivalent to Unity's Start()
void Init() {
    objects.reserve(100000);
    grid = SpatialGrid{ app.HalfWidth(), app.HalfHeight(), 2.0f * Config::Defaults::CircleRadius };

    constexpr float radius  = 5.0f;
    constexpr float spacing = radius * 2.0f;
    constexpr float startX  = -(99 * spacing / 2.0f);
    constexpr float startY  = -(99 * spacing / 2.0f);
    for (int x = 0; x < 100; x++) {
        for (int y = 0; y < 100; y++) {
            createObject(startX + static_cast<float>(x) * spacing, startY + static_cast<float>(y) * spacing, Circle{ radius, { 0.2f, 0.6f, 1.0f, 1.0f } });
        }
    }
}

void Update(float dt, Physics& physics) {
    for (auto& obj : objects) {
        obj.vy -= Config::Physics::Gravity * dt;
        obj.vx *= Config::Physics::Damping;
        obj.vy *= Config::Physics::Damping;
        obj.x  += obj.vx * dt;
        obj.y  += obj.vy * dt;
    }
    physics.Solve(objects, grid, app.HalfWidth(), app.HalfHeight(), dt);
}

int main(int, char**) {
    app.Init(Config::WindowTitle, Config::WindowWidth, Config::WindowHeight);
    Physics physics;   // ThreadPool starts here, safely after main() begins
    Init();

    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();

    profiler.MarkFrameStart();
    while (app.PollEvents()) {
        if (app.TakeF1Toggle()) {
            profilerOpen = !profilerOpen;
            app.SetProfilerOpen(profilerOpen);
        }

        auto  now = Clock::now();
        float dt  = std::min(std::chrono::duration<float>(now - last).count(), 1.0f / 30.0f);
        last      = now;

        Update(dt, physics);
        profiler.MarkComputeEnd();

        for (auto& obj : objects)
            obj.Draw(app);

        app.RenderFrame();
        profiler.MarkRenderEnd();
        app.SetObjectCount(static_cast<int>(objects.size()));
        if (profiler.Tick())
            app.SetProfilerStats(profiler.GetStats());

        profiler.MarkFrameStart();
    }
    app.Shutdown();
    return 0;
}
