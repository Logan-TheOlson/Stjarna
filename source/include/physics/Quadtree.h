#pragma once
#include <cstdint>
#include <vector>

#include "util/Vector.h"

namespace Gravity {

// Flattened, array-based quadtree: children of node i occupy indices
// [nodes[i].childStart, nodes[i].childStart + 4). Children always have a
// strictly larger index than their parent, so a simple reverse scan over
// `nodes` visits every node's children before the node itself.
// Highest total order (j+k) kept in the Cartesian multipole/local Taylor
// expansions used by the FMM path (Gravity::EvaluateFMM). M2L needs D-table
// entries up to order 2*FMMOrder, see FMM.cpp.
constexpr int FMMOrder = 3;

struct QuadNode {
    Vec2    center{};
    float   halfSize = 0.f;
    int32_t parent = -1;

    float   mass = 0.f;
    Vec2    centerOfMass{};
    // Traceless 3D quadrupole tensor components (with all masses in the
    // z=0 plane), needed because the chosen gravity law is a genuine 1/r^2
    // Newtonian force restricted to the simulation plane, not a native 2D
    // log-potential — qyy is NOT simply -qxx here. Used by the Barnes-Hut
    // (EvaluateForce) path only.
    float   qxx = 0.f, qxy = 0.f, qyy = 0.f;

    // Cartesian multipole moments about `center` (NOT centerOfMass — a fixed
    // geometric expansion point is what makes M2M translation vectors pure
    // tree geometry). multipole[j][k] is only meaningful for j+k<=FMMOrder;
    // used by the FMM path (Gravity::EvaluateFMM) only. double: order-6 terms
    // (see FMM.cpp's D-table) lose too much precision in float32 at the
    // separations coarse tree levels see.
    double  multipole[FMMOrder + 1][FMMOrder + 1] = {};

    int32_t childStart    = -1; // -1 = leaf
    int32_t particleStart = 0;
    int32_t particleCount = 0;
};

extern std::vector<QuadNode>  nodes;
extern std::vector<int32_t>   particleIndex; // permutation of [0,n) grouped by leaf

// Builds the tree over `pos[0..n)` (indices into `pos` are what get stored
// in `particleIndex` and later read back by Gravity::EvaluateForce).
void BuildQuadtree(const std::vector<Vec2>& pos, int32_t n);

// Bottom-up pass computing mass/center-of-mass/quadrupole for every node.
// Must be called after BuildQuadtree(), with the same `pos` array.
// Feeds the Barnes-Hut path (Gravity::EvaluateForce).
void ComputeMoments(const std::vector<Vec2>& pos);

// Bottom-up pass computing Cartesian multipole moments (P2M for leaves, M2M
// for internal nodes) for every node. Must be called after BuildQuadtree(),
// with the same `pos` array. Feeds the FMM path (Gravity::EvaluateFMM).
void ComputeMultipoles(const std::vector<Vec2>& pos);

} // namespace Gravity
