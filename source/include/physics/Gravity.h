#pragma once
#include <cstdint>
#include <vector>

#include "util/Vector.h"

namespace Gravity {

// Rebuilds the quadtree over pos[0..n) and computes its mass/COM/quadrupole
// moments. Call once per force pass before EvaluateForce()/DirectSum().
void BuildTree(const std::vector<Vec2>& pos, int32_t n);

// Barnes-Hut treecode force (monopole + quadrupole, softened, MAC-gated)
// on particle `selfIdx`. Requires BuildTree() to have been called with the
// same `pos` array.
Vec2 EvaluateForce(int32_t selfIdx, const std::vector<Vec2>& pos);

// O(n) direct pairwise summation, for validating EvaluateForce() against.
// Not meant for production use at full particle counts.
Vec2 DirectSumGravity(int32_t selfIdx, const std::vector<Vec2>& pos, int32_t n);

// O(n^2) total gravitational potential energy, -G * sum_{i<j} m_i*m_j / sqrt(r_ij^2 + softening^2).
// Intended for energy-drift monitoring on small particle counts only.
float DirectSumPotentialEnergy(const std::vector<Vec2>& pos, int32_t n);

// Compares EvaluateForce() against DirectSumGravity() over the first
// `sampleCount` particles and logs the max/mean relative error to stdout.
// Gated behind Config::Physics::ValidateGravity by the caller.
void ValidateAccuracy(const std::vector<Vec2>& pos, int32_t n, int32_t sampleCount);

// Compares a full EvaluateFMM() pass against DirectSumGravity() over the
// first `sampleCount` particles and logs the max/mean relative error to
// stdout. Gated behind Config::Physics::ValidateFMM by the caller.
void ValidateFMMAccuracy(const std::vector<Vec2>& pos, int32_t n, int32_t sampleCount);

} // namespace Gravity
