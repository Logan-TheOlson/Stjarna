#pragma once
#include <cstdint>
#include <vector>

#include "util/Vector.h"

namespace Gravity {

// Flattened, array-based quadtree: children of node i occupy indices
// [nodes[i].childStart, nodes[i].childStart + 4). Children always have a
// strictly larger index than their parent, so a simple reverse scan over
// `nodes` visits every node's children before the node itself.
struct QuadNode {
    Vec2    center{};
    float   halfSize = 0.f;
    int32_t parent = -1;

    float   mass = 0.f;
    Vec2    centerOfMass{};
    // Traceless 3D quadrupole tensor components (with all masses in the
    // z=0 plane), needed because EvaluateForce()'s gravity law is a genuine
    // 1/r^2 Newtonian force restricted to the simulation plane, not the
    // native 2D log-potential law EvaluateForce2D() uses instead — qyy is
    // NOT simply -qxx here. Used by the Barnes-Hut (EvaluateForce) path
    // only; EvaluateForce2D() is monopole-only (mass/centerOfMass above),
    // since this tensor isn't valid for its 1/r law.
    float   qxx = 0.f, qxy = 0.f, qyy = 0.f;

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
// Must be called after BuildQuadtree(), with the same `pos` array. Feeds
// both the Barnes-Hut (Gravity::EvaluateForce) and genuine-2D
// (Gravity::EvaluateForce2D) paths — the latter only reads mass/centerOfMass.
void ComputeMoments(const std::vector<Vec2>& pos);

} // namespace Gravity
