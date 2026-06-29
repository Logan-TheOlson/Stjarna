#pragma once
#include "Solver.h"

class ImpulseSolver : public Solver {
public:
    void        Solve(std::span<Object> objects, SpatialGrid& grid, float hw, float hh, float dt) override;
    const char* Name() const override { return "Impulse"; }
};
