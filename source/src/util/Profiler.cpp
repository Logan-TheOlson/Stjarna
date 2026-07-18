#include "util/Profiler.h"

void Profiler::MarkFrameStart() { frameStart_ = Clock::now(); }
void Profiler::MarkComputeEnd() { computeEnd_ = Clock::now(); }

void Profiler::MarkRenderEnd() {
    auto now = Clock::now();
    auto ms  = [](auto d) { return std::chrono::duration<float, std::milli>(d).count(); };
    accFrame_   += ms(now - frameStart_);
    accCompute_ += ms(computeEnd_ - frameStart_);
    accRender_  += ms(now - computeEnd_);
    ++count_;
}

bool Profiler::Tick() {
    if (count_ < kSamples) return false;
    const float inv    = 1.0f / count_;
    const float frame  = accFrame_   * inv;
    stats_.fps       = 1000.0f / frame;
    stats_.frameMs   = frame;
    stats_.computeMs = accCompute_ * inv;
    stats_.renderMs  = accRender_  * inv;
    accFrame_ = accCompute_ = accRender_ = 0.0f;
    count_ = 0;
    return true;
}
