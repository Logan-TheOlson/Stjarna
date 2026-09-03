#pragma once
#include <cstdint>
#include <functional>

// Splits [0, total) into one chunk per worker thread and blocks until fn(begin, end) has run for
// every chunk. Not std::execution::par: this project's MinGW/libstdc++ has no TBB backend, so
// that silently runs sequentially (measured slower than std::execution::seq).
void ParallelFor(int32_t total, const std::function<void(int32_t, int32_t)>& fn);
