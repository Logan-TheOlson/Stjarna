#include "engine/Engine.h"
#include "renderer/App.h"
#include "util/Profiler.h"
#include "physics/Gravity.h"
#include "Config.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

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

static void Integrate(float subDt) {
    for (auto& obj : objects) {
        obj.vel += obj.acc * subDt;
        obj.pos += obj.vel * subDt;
    }
}

// One physics frame: Substeps sub-updates, each recomputing forces only
// every ForceInterval-th substep (see Config::Physics::ForceInterval).
static void StepFrame(float dt) {
    const float subDt = dt / static_cast<float>(Config::Physics::Substeps);
    for (int step = 0; step < Config::Physics::Substeps; step++) {
        if (step % Config::Physics::ForceInterval == 0) {
            for (auto& obj : objects) obj.acc = Vec2(0.0f, 0.0f);
            Update(subDt);
        }
        Integrate(subDt);
    }
}

static void RunLive() {
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
        dt *= app.TimeScale();

        StepFrame(dt);
        profiler.MarkComputeEnd();

        for (auto& obj : objects)
            obj.Draw(app);

        app.RenderFrame();
        profiler.MarkRenderEnd();
        app.SetObjectCount(static_cast<int>(objects.size()));

        float kineticEnergy = 0.f;
        for (auto& obj : objects)
            kineticEnergy += 0.5f * (obj.vel.x * obj.vel.x + obj.vel.y * obj.vel.y);
        app.SetKineticEnergy(kineticEnergy);

        if (objects.size() <= Config::Debug::EnergyMonitorMaxParticles) {
            std::vector<Vec2> pos;
            pos.reserve(objects.size());
            for (auto& obj : objects) pos.push_back(obj.pos);
            const float potentialEnergy = Gravity::DirectSumPotentialEnergy(pos, static_cast<int32_t>(pos.size()));
            app.SetTotalEnergy(kineticEnergy + potentialEnergy, true);
        } else {
            app.SetTotalEnergy(0.f, false);
        }

        if (profiler.Tick())
            app.SetProfilerStats(profiler.GetStats());

        profiler.MarkFrameStart();
    }
}

// Renders `durationSeconds` of simulated time at a fixed timestep (not tied
// to wall clock) and pipes raw BGRA8 frames straight into an ffmpeg process
// via stdin — no PNG-sequence intermediate, so the finished video exists the
// moment the loop ends. Not bound by real-time performance: unlike RunLive(),
// a slow physics step here just makes the export take longer, it doesn't
// affect the output video's frame rate.
static void RunPrecompute(float durationSeconds, int fps, const std::string& outputPath) {
    const float dt          = 1.0f / static_cast<float>(fps);
    const int   totalFrames = static_cast<int>(durationSeconds * fps + 0.5f);

    app.EnableCapture(true);

    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -f rawvideo -pixel_format bgra -video_size %dx%d -framerate %d -i - "
        "-pix_fmt yuv420p -c:v libx264 -preset fast -crf 18 \"%s\"",
        app.Width(), app.Height(), fps, outputPath.c_str());

    FILE* ffmpeg = _popen(cmd, "wb");
    if (!ffmpeg) {
        std::fprintf(stderr, "Failed to launch ffmpeg (is it installed and on PATH?)\n");
        app.EnableCapture(false);
        return;
    }

    std::printf("Precomputing %d frames (%.1fs simulated @ %dfps) -> %s\n", totalFrames, durationSeconds, fps, outputPath.c_str());

    for (int frame = 0; frame < totalFrames; frame++) {
        if (!app.PollEvents()) {
            std::printf("Window closed, aborting early at frame %d/%d\n", frame, totalFrames);
            break;
        }

        StepFrame(dt);

        for (auto& obj : objects)
            obj.Draw(app);
        app.RenderFrame();

        std::fwrite(app.CapturedPixelData(), 1, app.CapturedPixelDataSize(), ffmpeg);

        if (frame % fps == 0)
            std::printf("  %d / %d frames (%.0f%%)\n", frame, totalFrames, 100.0 * frame / totalFrames);
    }

    _pclose(ffmpeg);
    app.EnableCapture(false);
    std::printf("Done: %s\n", outputPath.c_str());
}

int main(int, char**) {
    std::printf("Stjarna\n");
    std::printf("  [1] Live (interactive)\n");
    std::printf("  [2] Precompute (render straight to a video file)\n");
    std::printf("Select mode [1]: ");
    std::fflush(stdout);

    std::string line;
    std::getline(std::cin, line);
    const bool precompute = !line.empty() && line[0] == '2';

    float       precomputeDuration = 10.f;
    int         precomputeFps      = 60;
    std::string precomputeOutput   = "stjarna.mp4";

    if (precompute) {
        std::printf("Duration in seconds [%.0f]: ", precomputeDuration);
        std::fflush(stdout);
        std::getline(std::cin, line);
        if (!line.empty()) precomputeDuration = std::stof(line);

        std::printf("Output fps [%d]: ", precomputeFps);
        std::fflush(stdout);
        std::getline(std::cin, line);
        if (!line.empty()) precomputeFps = std::stoi(line);

        std::printf("Output file [%s]: ", precomputeOutput.c_str());
        std::fflush(stdout);
        std::getline(std::cin, line);
        if (!line.empty()) precomputeOutput = line;
    }

    app.Init(Config::WindowTitle, Config::WindowWidth, Config::WindowHeight);

    objects.reserve(100000);

    Init();

    if (precompute)
        RunPrecompute(precomputeDuration, precomputeFps, precomputeOutput);
    else
        RunLive();

    app.Shutdown();
    return 0;
}
