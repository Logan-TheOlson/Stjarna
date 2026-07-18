#include "Engine.h"
#include "app/App.h"
#include "Profiler.h"
#include "Config.h"
#include <algorithm>
#include <chrono>

// User-defined entry points
void Init();
void Update(float dt);

std::vector<Object> objects;

static App      app;
static Profiler profiler;
static bool     profilerOpen = false;

Object& CreateObject(float x, float y, Circle shape) {
    objects.push_back({ .x = x, .y = y, .shape = shape });
    return objects.back();
}

Object& CreateObject(float x, float y, Rectangle shape) {
    objects.push_back({ .x = x, .y = y, .shape = shape });
    return objects.back();
}

void RemoveObject(size_t i) {
    objects[i] = objects.back();
    objects.pop_back();
}

float ScreenHalfWidth()  { return app.HalfWidth(); }
float ScreenHalfHeight() { return app.HalfHeight(); }

static void Integrate(float dt) {
    const float subDt = dt / static_cast<float>(Config::Physics::Substeps);
    for (auto& obj : objects) {
        for (int step = 0; step < Config::Physics::Substeps; step++) {
            obj.vx += obj.ax * subDt;
            obj.vy += obj.ay * subDt;
            obj.x  += obj.vx * subDt;
            obj.y  += obj.vy * subDt;
        }
        obj.ax = obj.ay = 0.0f;
    }
}

int main(int, char**) {
    app.Init(Config::WindowTitle, Config::WindowWidth, Config::WindowHeight);

    objects.reserve(100000);

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

        Update(dt);
        Integrate(dt);
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
