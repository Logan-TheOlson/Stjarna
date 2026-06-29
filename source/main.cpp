#include "app/App.h"
#include "Object.h"
#include "physics/collision_Engine.h"
#include "Profiler.h"
#include "Config.h"
#include <chrono>
#include <deque>

App                app;
std::deque<Object> objects;
CollisionEngine    collision;
Profiler           profiler;
bool               profilerOpen = false;

Object& createObject(float x, float y, const Circle& shape) {
    objects.push_back({ x, y, 0.0f, 0.0f, shape });
    return objects.back();
}

void Update(float dt) {
    const int n = static_cast<int>(objects.size());
    for (int i = 0; i < n; i++) {
        Object& obj = objects[i];
        obj.vy -= Config::Physics::Gravity * dt;
        obj.x  += obj.vx * dt;
        obj.y  += obj.vy * dt;
        collision.ResolveBoundary(obj);
        obj.Draw(app);
    }
}

void Init() {
    collision = { app.HalfWidth(), app.HalfHeight() };

    createObject(   0.0f,  200.0f, Circle{ 15.0f, { 1.0f, 0.5f, 0.1f, 1.0f } });
    createObject( 120.0f,  300.0f, Circle{ 20.0f, { 0.2f, 0.6f, 1.0f, 1.0f } });
    createObject(-150.0f,  100.0f, Circle{ 12.0f, { 0.4f, 1.0f, 0.3f, 1.0f } });
}

int main(int, char**) {
    app.Init(Config::WindowTitle, Config::WindowWidth, Config::WindowHeight);
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

        collision = { app.HalfWidth(), app.HalfHeight() };

        Update(dt);
        profiler.MarkComputeEnd();

        app.RenderFrame();

        profiler.MarkRenderEnd();
        if (profiler.Tick())
            app.SetProfilerStats(profiler.GetStats());

        profiler.MarkFrameStart();
    }
    app.Shutdown();
    return 0;
}
