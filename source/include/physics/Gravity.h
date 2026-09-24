#pragma once
#include <cstdint>
#include <vector>

#include "physics/GravityParams.h"
#include "util/Vector.h"

namespace Gravity {

// Applies `p` (G, particle mass, softening, MAC theta, leaf size) to every gravity path below.
// Call once whenever a scene loads (LoadScene()), before BuildTree/BuildQuadtree — mirrors
// main.cpp's RecomputeSphConstants() for the SPH kernel constants.
void SetParams(const GravityParams& p);

// Rebuilds the quadtree over pos[0..n) and computes its mass/COM/quadrupole
// moments. Call once per force pass before EvaluateForce()/DirectSum().
void BuildTree(const std::vector<Vec2>& pos, int32_t n);

// Barnes-Hut treecode force (monopole + quadrupole, softened, MAC-gated)
// on particle `selfIdx`, for the 3D Newtonian (1/r^2) law restricted to the
// simulation plane. Requires BuildTree() to have been called with the same
// `pos` array.
Vec2 EvaluateForce(int32_t selfIdx, const std::vector<Vec2>& pos);

// Barnes-Hut treecode force for the genuine 2D (log-potential, 1/r) law a
// real 2D universe's Poisson equation gives — see EvaluateForce2D's .cpp
// comment for the derivation. Monopole-only: QuadNode's quadrupole tensor is
// only valid for EvaluateForce()'s law (see Quadtree.h). Requires
// BuildTree() to have been called with the same `pos` array.
Vec2 EvaluateForce2D(int32_t selfIdx, const std::vector<Vec2>& pos);

// O(n) direct pairwise summation, for validating EvaluateForce() against.
// Not meant for production use at full particle counts.
Vec2 DirectSumGravity(int32_t selfIdx, const std::vector<Vec2>& pos, int32_t n);

// O(n) direct pairwise summation of the genuine 2D law, for validating
// EvaluateForce2D() against. Not meant for production use at full particle
// counts.
Vec2 DirectSumGravity2D(int32_t selfIdx, const std::vector<Vec2>& pos, int32_t n);

// O(n^2) total gravitational potential energy for the 3D-restricted-to-the-plane (1/r^2 force)
// law, -G * sum_{i<j} m_i*m_j / sqrt(r_ij^2 + softening^2) — matches EvaluateForce()/
// DirectSumGravity(). Intended for energy-drift monitoring on small particle counts only.
float DirectSumPotentialEnergy(const std::vector<Vec2>& pos, int32_t n);

// The genuine-2D counterpart of DirectSumPotentialEnergy() above, for the log-potential (1/r
// force) law: G * sum_{i<j} m_i*m_j * ln(r_ij^2 + softening^2) — matches EvaluateForce2D()/
// DirectSumGravity2D(). Using DirectSumPotentialEnergy() while Genuine2D is the active method (as
// it is for every scene that enables gravity) gives a physically meaningless energy-drift plot.
float DirectSumPotentialEnergy2D(const std::vector<Vec2>& pos, int32_t n);

// Compares EvaluateForce() against DirectSumGravity() over the first
// `sampleCount` particles and logs the max/mean relative error to stdout.
// Gated behind the active scene's gravity.validate by the caller.
void ValidateAccuracy(const std::vector<Vec2>& pos, int32_t n, int32_t sampleCount);

// Compares EvaluateForce2D() against DirectSumGravity2D() over the first
// `sampleCount` particles and logs the max/mean relative error to stdout.
// Gated behind the active scene's gravity.validate by the caller.
void ValidateAccuracy2D(const std::vector<Vec2>& pos, int32_t n, int32_t sampleCount);

} // namespace Gravity
