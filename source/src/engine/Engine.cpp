#include "engine/Engine.h"
#include "renderer/App.h"
#include "util/Profiler.h"
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

Object& CreateObject(float x, float y, Renderable r) {
    objects.push_back({ .pos = Vec2(x, y), .renderable = r });
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
            obj.vel += obj.acc * subDt;
            obj.pos += obj.vel * subDt;
        }
        obj.acc = Vec2(0.0f, 0.0f);
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
