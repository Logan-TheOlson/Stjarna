#pragma once
#include <cstdint>
#include <vector>

#include "util/Vector.h"

namespace Gravity {

// Order-FMMOrder Cartesian multipole/local FMM self-gravity: an O(N)
// alternative to the Barnes-Hut path (EvaluateForce) built on the same
// quadtree. Requires BuildQuadtree() + ComputeMultipoles() to have been
// called first, with the same `pos` array passed here. Writes one
// acceleration per particle into outAccel (resized to n, overwritten, not
// accumulated).
void EvaluateFMM(const std::vector<Vec2>& pos, int32_t n, std::vector<Vec2>& outAccel);

} // namespace Gravity
