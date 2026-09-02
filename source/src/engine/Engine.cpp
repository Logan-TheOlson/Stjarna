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

static void resolveAxis(float& p, float& v, float half, float r) {
    if (p - r < -half) { p = -half + r; if (v < 0.0f) v *= -Config::Physics::Restitution; }
    if (p + r >  half) { p =  half - r; if (v > 0.0f) v *= -Config::Physics::Restitution; }
}

// Integrates a single substep, reusing whatever force (particle.acc) the last Update() call
// computed — held constant across Config::Physics::ForceInterval substeps at a time to trade
// some staleness back for compute cost. acc is reset only when it's about to be recomputed
// (see the main loop), not here.
static void Integrate(float subDt) {
    const float hw = ScreenHalfWidth(), hh = ScreenHalfHeight();
    for (auto& obj : objects) {
        const float r = obj.Radius();
        obj.vel += obj.acc * subDt;
        obj.pos += obj.vel * subDt;
        resolveAxis(obj.pos.x, obj.vel.x, hw, r);
        resolveAxis(obj.pos.y, obj.vel.y, hh, r);
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

        // Force is recomputed every ForceInterval substeps (not once per frame, not every
        // substep) — see Integrate()'s comment for why a stale force is a real energy-gain
        // source, and why recomputing every substep was too expensive at this particle count.
        const float subDt = dt / static_cast<float>(Config::Physics::Substeps);
        for (int step = 0; step < Config::Physics::Substeps; step++) {
            if (step % Config::Physics::ForceInterval == 0) {
                for (auto& obj : objects) obj.acc = Vec2(0.0f, 0.0f);
                Update(subDt);
            }
            Integrate(subDt);
        }
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
