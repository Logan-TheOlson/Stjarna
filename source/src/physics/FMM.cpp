#include "physics/FMM.h"

#include <array>
#include <cmath>
#include <mutex>
#include <utility>

#include "Config.h"
#include "physics/Combinatorics.h"
#include "physics/Quadtree.h"
#include "util/ThreadPool.h"

// Cartesian Taylor-expansion FMM for the softened 1/r Newtonian kernel.
// The multipole (P2M/M2M), M2L, L2L formulas and the force sign were all
// derived and numerically cross-checked against direct summation before
// being transcribed here (see FMM design discussion) — in particular M2L
// needs an alternating (-1)^(j'+k') sign and Binomial(j+j',j)*Binomial(k+k',k)
// weights that a naive "L[j,k] += M[j',k'] * D[j+j',k+k']" formula omits.
//
// All multipole/local expansion math is done in double: the D-table's
// order-6 terms involve dx^6-scale quantities at the separations coarse
// tree levels see, and float32 there was empirically measured to cause
// 30-80% force errors that grew with N/tree depth — fixed by switching to
// double (see FMM design discussion).

namespace Gravity {

namespace {
    constexpr int P       = FMMOrder;      // 3
    constexpr int DOrder  = 2 * FMMOrder;  // 6: highest D-table order M2L needs

    constexpr double G    = Config::Physics::G;
    constexpr double Mass = Config::Physics::ParticleMass;
    constexpr double Eps2 = static_cast<double>(Config::Physics::GravSoftening) * Config::Physics::GravSoftening;
    constexpr double Theta = Config::Physics::FMMTheta;

    // std::array (not a raw double[][]) so std::vector<LocalTable> can assign/fill it.
    using LocalTable = std::array<std::array<double, P + 1>, P + 1>;

    double Pow(double base, int exp) {
        double result = 1.0;
        for (int i = 0; i < exp; i++) result *= base;
        return result;
    }

    // Mixed partial derivatives of 1/r (softened), normalized by 1/(m!n!):
    // DTab[m][n], valid for m+n<=DOrder. Closed forms derived symbolically
    // (Wolfram) and verified against the raw derivatives to ~1e-15 relative
    // error; see the FMM design discussion for the derivation.
    void ComputeDTable(double dx, double dy, double DTab[DOrder + 1][DOrder + 1], double eps2 = Eps2) {
        const double r2     = dx*dx + dy*dy + eps2;
        const double invR2  = 1.0 / r2;
        const double invR1  = std::sqrt(invR2);
        const double invR3  = invR1 * invR2;
        const double invR5  = invR3 * invR2;
        const double invR7  = invR5 * invR2;
        const double invR9  = invR7 * invR2;
        const double invR11 = invR9 * invR2;
        const double invR13 = invR11 * invR2;

        const double dx2=dx*dx, dx3=dx2*dx, dx4=dx3*dx, dx5=dx4*dx, dx6=dx5*dx;
        const double dy2=dy*dy, dy3=dy2*dy, dy4=dy3*dy, dy5=dy4*dy, dy6=dy5*dy;

        DTab[0][0] = invR1;
        DTab[0][1] = -dy * invR3;
        DTab[1][0] = -dx * invR3;
        DTab[0][2] = (-0.5*dx2 + dy2) * invR5;
        DTab[1][1] = (3.0*dx*dy) * invR5;
        DTab[2][0] = (dx2 - 0.5*dy2) * invR5;
        DTab[0][3] = (1.5*dx2*dy - dy3) * invR7;
        DTab[1][2] = (1.5*dx3 - 6.0*dx*dy2) * invR7;
        DTab[2][1] = (-6.0*dx2*dy + 1.5*dy3) * invR7;
        DTab[3][0] = (-dx3 + 1.5*dx*dy2) * invR7;
        DTab[0][4] = (0.375*dx4 - 3.0*dx2*dy2 + dy4) * invR9;
        DTab[1][3] = (-7.5*dx3*dy + 10.0*dx*dy3) * invR9;
        DTab[2][2] = (-3.0*dx4 + 20.25*dx2*dy2 - 3.0*dy4) * invR9;
        DTab[3][1] = (10.0*dx3*dy - 7.5*dx*dy3) * invR9;
        DTab[4][0] = (dx4 - 3.0*dx2*dy2 + 0.375*dy4) * invR9;
        DTab[0][5] = (-1.875*dx4*dy + 5.0*dx2*dy3 - dy5) * invR11;
        DTab[1][4] = (-1.875*dx5 + 22.5*dx3*dy2 - 15.0*dx*dy4) * invR11;
        DTab[2][3] = (22.5*dx4*dy - 51.25*dx2*dy3 + 5.0*dy5) * invR11;
        DTab[3][2] = (5.0*dx5 - 51.25*dx3*dy2 + 22.5*dx*dy4) * invR11;
        DTab[4][1] = (-15.0*dx4*dy + 22.5*dx2*dy3 - 1.875*dy5) * invR11;
        DTab[5][0] = (-dx5 + 5.0*dx3*dy2 - 1.875*dx*dy4) * invR11;
        DTab[0][6] = (-0.3125*dx6 + 5.625*dx4*dy2 - 7.5*dx2*dy4 + dy6) * invR13;
        DTab[1][5] = (13.125*dx5*dy - 52.5*dx3*dy3 + 21.0*dx*dy5) * invR13;
        DTab[2][4] = (5.625*dx6 - 94.6875*dx4*dy2 + 108.75*dx2*dy4 - 7.5*dy6) * invR13;
        DTab[3][3] = (-52.5*dx5*dy + 183.75*dx3*dy3 - 52.5*dx*dy5) * invR13;
        DTab[4][2] = (-7.5*dx6 + 108.75*dx4*dy2 - 94.6875*dx2*dy4 + 5.625*dy6) * invR13;
        DTab[5][1] = (21.0*dx5*dy - 52.5*dx3*dy3 + 13.125*dx*dy5) * invR13;
        DTab[6][0] = (dx6 - 7.5*dx4*dy2 + 5.625*dx2*dy4 - 0.3125*dy6) * invR13;
    }

    // L[j,k] (about targetCenter) += Sum_{j',k'} (-1)^(j'+k') * C(j+j',j) * C(k+k',k)
    //                                            * M[j',k'] * D[j+j',k+k'](R)
    // where R = targetCenter - sourceCenter (the multipole's own center).
    void M2L(const double M[P + 1][P + 1], const double DTab[DOrder + 1][DOrder + 1], LocalTable& outLocal) {
        for (int j = 0; j <= P; j++) {
            for (int k = 0; k + j <= P; k++) {
                double acc = 0.0;
                for (int jp = 0; jp <= P; jp++) {
                    for (int kp = 0; kp + jp <= P; kp++) {
                        const double sign = ((jp + kp) & 1) ? -1.0 : 1.0;
                        acc += sign * Binomial(j + jp, j) * Binomial(k + kp, k) * M[jp][kp] * DTab[j + jp][k + kp];
                    }
                }
                outLocal[j][k] += acc;
            }
        }
    }

    // childLocal[jp,kp] += Sum_{j>=jp,k>=kp,j+k<=P} parentLocal[j,k] * C(j,jp)*C(k,kp) * s.x^(j-jp) * s.y^(k-kp)
    // where s = childCenter - parentCenter.
    void L2LAdd(const LocalTable& parentLocal, double sx, double sy, LocalTable& childLocal) {
        for (int jp = 0; jp <= P; jp++) {
            for (int kp = 0; kp + jp <= P; kp++) {
                double acc = 0.0;
                for (int j = jp; j <= P; j++) {
                    const double sxTerm = Pow(sx, j - jp);
                    for (int k = kp; k + j <= P; k++) {
                        acc += parentLocal[j][k] * Binomial(j, jp) * Binomial(k, kp) * sxTerm * Pow(sy, k - kp);
                    }
                }
                childLocal[jp][kp] += acc;
            }
        }
    }

    // Attractive acceleration direction (validated numerically): grad_u of the
    // local Taylor polynomial Sum L[j,k] u.x^j u.y^k already points toward the
    // source mass, matching this codebase's `accel -= G*mass*diff/r^3` (diff =
    // field - source) sign convention used elsewhere — no extra minus needed.
    void EvalGradient(const LocalTable& L, double ux, double uy, double& gx, double& gy) {
        gx = 0.0; gy = 0.0;
        for (int j = 0; j <= P; j++) {
            for (int k = 0; k + j <= P; k++) {
                if (j >= 1) gx += L[j][k] * static_cast<double>(j) * Pow(ux, j - 1) * Pow(uy, k);
                if (k >= 1) gy += L[j][k] * static_cast<double>(k) * Pow(ux, j) * Pow(uy, k - 1);
            }
        }
    }

    // Bundles the per-call mutable state DualTraverse writes into. Passed by
    // reference through the recursion instead of using shared globals so that
    // separate top-level branches can be run on separate threads, each with
    // its own private ctx (see EvaluateFMM), and merged afterward — the tree
    // itself (`nodes`/`particleIndex`) is read-only during traversal so needs
    // no such duplication.
    struct TraverseCtx {
        const std::vector<Vec2>* pos;
        std::vector<Vec2>*        accel;    // near-field (P2P) contributions
        std::vector<LocalTable>*  localExp; // far-field (M2L) contributions, per node
    };

    void ApplyPairForce(TraverseCtx& ctx, int32_t pi, int32_t pj) {
        const Vec2  d   = (*ctx.pos)[pi] - (*ctx.pos)[pj];
        const float r2  = dot(d, d) + static_cast<float>(Eps2);
        const float invR3 = 1.f / (r2 * std::sqrt(r2));
        const Vec2  f   = (static_cast<float>(G * Mass)) * d * invR3;
        (*ctx.accel)[pi] -= f;
        (*ctx.accel)[pj] += f;
    }

    void LeafSelfP2P(TraverseCtx& ctx, int32_t a) {
        const QuadNode& A = nodes[a];
        for (int32_t i = 0; i < A.particleCount; i++) {
            const int32_t pi = particleIndex[A.particleStart + i];
            for (int32_t j = i + 1; j < A.particleCount; j++) {
                ApplyPairForce(ctx, pi, particleIndex[A.particleStart + j]);
            }
        }
    }

    void LeafPairP2P(TraverseCtx& ctx, int32_t a, int32_t b) {
        const QuadNode& A = nodes[a];
        const QuadNode& B = nodes[b];
        for (int32_t i = 0; i < A.particleCount; i++) {
            const int32_t pi = particleIndex[A.particleStart + i];
            for (int32_t j = 0; j < B.particleCount; j++) {
                ApplyPairForce(ctx, pi, particleIndex[B.particleStart + j]);
            }
        }
    }

    // Mutual (self-tree) dual traversal: for every pair of nodes, either they
    // are well-separated (accumulate M2L into both sides' local expansions),
    // both leaves and too close (direct P2P), or one side gets refined into
    // its children and we recurse.
    void DualTraverse(TraverseCtx& ctx, int32_t a, int32_t b) {
        const QuadNode& A = nodes[a];
        const QuadNode& B = nodes[b];

        if (a == b) {
            if (A.childStart < 0) {
                LeafSelfP2P(ctx, a);
            } else {
                for (int32_t i = 0; i < 4; i++)
                    for (int32_t j = i; j < 4; j++)
                        DualTraverse(ctx, A.childStart + i, A.childStart + j);
            }
            return;
        }

        const Vec2  diff = A.center - B.center;
        const double dist = static_cast<double>(magnitude(diff));

        // Bounding-CIRCLE radius (halfSize*sqrt2, corner distance), not half-side
        // length: a square cell's worst-case point is at its corner, and using
        // the edge distance here made the MAC accept insufficiently-separated
        // pairs, causing large p=3 truncation error at the default theta.
        constexpr double Sqrt2 = 1.4142135623730951;
        const double radiusA = A.halfSize * Sqrt2;
        const double radiusB = B.halfSize * Sqrt2;

        if ((radiusA + radiusB) < Theta * dist) {
            double DTab[DOrder + 1][DOrder + 1];
            const Vec2 Rab = B.center - A.center;
            ComputeDTable(Rab.x, Rab.y, DTab);
            M2L(A.multipole, DTab, (*ctx.localExp)[b]);

            const Vec2 Rba = A.center - B.center;
            ComputeDTable(Rba.x, Rba.y, DTab);
            M2L(B.multipole, DTab, (*ctx.localExp)[a]);
            return;
        }

        const bool aLeaf = A.childStart < 0;
        const bool bLeaf = B.childStart < 0;
        if (aLeaf && bLeaf) {
            LeafPairP2P(ctx, a, b);
            return;
        }

        if (bLeaf || (!aLeaf && A.halfSize >= B.halfSize)) {
            for (int32_t i = 0; i < 4; i++) DualTraverse(ctx, A.childStart + i, b);
        } else {
            for (int32_t j = 0; j < 4; j++) DualTraverse(ctx, a, B.childStart + j);
        }
    }

    void MergeInto(std::vector<LocalTable>& dstLocal, std::vector<Vec2>& dstAccel,
                   const std::vector<LocalTable>& srcLocal, const std::vector<Vec2>& srcAccel) {
        for (size_t ni = 0; ni < dstLocal.size(); ni++)
            for (int j = 0; j <= P; j++)
                for (int k = 0; k + j <= P; k++)
                    dstLocal[ni][j][k] += srcLocal[ni][j][k];
        for (size_t pi = 0; pi < dstAccel.size(); pi++)
            dstAccel[pi] += srcAccel[pi];
    }
}

void EvaluateFMM(const std::vector<Vec2>& pos, int32_t n, std::vector<Vec2>& outAccel) {
    outAccel.assign(n, Vec2(0.f, 0.f));
    if (nodes.empty()) return;

    std::vector<LocalTable> localExp(nodes.size(), LocalTable{});

    const QuadNode& root = nodes[0];
    if (root.childStart < 0) {
        // Small enough that root never split: nothing meaningful to parallelize.
        TraverseCtx ctx{ &pos, &outAccel, &localExp };
        DualTraverse(ctx, 0, 0);
    } else {
        // Enumerate the same 10 (i<=j) pairs among root's children that the
        // a==b branch of DualTraverse would otherwise expand serially, and run
        // them on separate threads. Different pairs can recurse into and write
        // the same descendant node's local expansion (e.g. pairs (0,1) and
        // (0,2) both touch child 0's subtree), so each chunk accumulates into
        // its own private localExp/accel copy and results are summed after —
        // safe because M2L/P2P contributions are purely additive.
        std::vector<std::pair<int32_t, int32_t>> pairs;
        pairs.reserve(10);
        for (int32_t i = 0; i < 4; i++)
            for (int32_t j = i; j < 4; j++)
                pairs.emplace_back(root.childStart + i, root.childStart + j);

        std::mutex mergeMutex;
        ParallelFor(static_cast<int32_t>(pairs.size()), [&](int32_t begin, int32_t end) {
            std::vector<LocalTable> localScratch(nodes.size(), LocalTable{});
            std::vector<Vec2>       accelScratch(n, Vec2(0.f, 0.f));
            TraverseCtx ctx{ &pos, &accelScratch, &localScratch };

            for (int32_t idx = begin; idx < end; idx++)
                DualTraverse(ctx, pairs[idx].first, pairs[idx].second);

            std::lock_guard<std::mutex> lock(mergeMutex);
            MergeInto(localExp, outAccel, localScratch, accelScratch);
        });
    }

    // Top-down L2L: parent index is always smaller than any child's (see
    // BuildQuadtree), so a single forward scan already finalizes every
    // node's local expansion before its children shift it down.
    for (int32_t ni = 1; ni < static_cast<int32_t>(nodes.size()); ni++) {
        const QuadNode& node = nodes[ni];
        const int32_t   pi   = node.parent;
        if (pi < 0) continue;
        const Vec2 s = node.center - nodes[pi].center;
        L2LAdd(localExp[pi], s.x, s.y, localExp[ni]);
    }

    for (int32_t ni = 0; ni < static_cast<int32_t>(nodes.size()); ni++) {
        const QuadNode& node = nodes[ni];
        if (node.childStart >= 0) continue; // leaves only
        for (int32_t k = 0; k < node.particleCount; k++) {
            const int32_t pidx = particleIndex[node.particleStart + k];
            const Vec2    u    = pos[pidx] - node.center;
            double gx, gy;
            EvalGradient(localExp[ni], u.x, u.y, gx, gy);
            outAccel[pidx] += Vec2(static_cast<float>(G * gx), static_cast<float>(G * gy));
        }
    }
}

} // namespace Gravity
