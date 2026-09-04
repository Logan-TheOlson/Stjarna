#pragma once

// Small shared helpers for the Cartesian multipole/local expansion math
// (M2M/M2L/L2L), used by both Quadtree.cpp and FMM.cpp. Only needs n up to
// 2*FMMOrder (currently 6), so a tiny fixed table is simplest and fastest.
namespace Gravity {

// double: the multipole/local expansion math involves high powers (up to 6)
// of node separations that can be large at coarse tree levels, and float32
// loses too much precision there (empirically ~30-80% force error at 10k
// particles; verified fixed by switching to double).
inline double Factorial(int n) {
    static constexpr double table[] = { 1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0 };
    return table[n];
}

inline double Binomial(int n, int k) {
    if (k < 0 || k > n) return 0.0;
    return Factorial(n) / (Factorial(k) * Factorial(n - k));
}

} // namespace Gravity
