#pragma once
#include <chrono>

class Profiler {
    using Clock = std::chrono::steady_clock;
    using TP    = Clock::time_point;
    static constexpr int kSamples = 60;
public:
    struct Stats {
        float fps       = 0.f;
        float frameMs   = 0.f;
        float computeMs = 0.f;
        float renderMs  = 0.f;
    };

    void MarkFrameStart();
    void MarkComputeEnd();
    void MarkRenderEnd();
    bool         Tick();
    const Stats& GetStats() const { return stats_; }
private:
    TP    frameStart_{}, computeEnd_{};
    float accFrame_{ 0 }, accCompute_{ 0 }, accRender_{ 0 };
    int   count_{ 0 };
    Stats stats_;
};
