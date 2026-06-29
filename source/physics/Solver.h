#pragma once
#include <span>

struct Object;
class SpatialGrid;

class Solver {
public:
    virtual ~Solver() = default;
    virtual void        Solve(std::span<Object> objects, SpatialGrid& grid, float hw, float hh, float dt) = 0;
    virtual const char* Name() const = 0;
};
