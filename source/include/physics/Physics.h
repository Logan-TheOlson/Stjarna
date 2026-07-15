#pragma once
#include <span>

struct Object;
class SpatialGrid;

class Physics {
public:
    // Forces — accumulate into object.ax / object.ay
    static void ApplyGravity(std::span<Object> objects);
    static void ApplyForce(Object& obj, float fx, float fy);

    // Constraints — call after forces, before or after grid rebuild
    static void ResolveBoundaries(std::span<Object> objects, float hw, float hh);
    static void ResolveCollisions(std::span<Object> objects, const SpatialGrid& grid);

private:
    static void ResolveCell(std::span<Object> objects, const SpatialGrid& grid, int col, int row);
};
