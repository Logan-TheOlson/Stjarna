#pragma once
#include <chrono>
#include <string>

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
    void SetTag(const char* tag) { tag_ = tag; }
    bool        Tick();    // returns true once per kSamples frames when stats refresh
    const Stats& GetStats() const { return stats_; }
private:
    TP    frameStart_{}, computeEnd_{};
    float accFrame_{ 0 }, accCompute_{ 0 }, accRender_{ 0 };
    int   count_{ 0 };
    std::string tag_;
    Stats stats_;
};
