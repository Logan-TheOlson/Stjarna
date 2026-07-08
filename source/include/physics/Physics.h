#pragma once
#include "SpatialGrid.h"
#include "ThreadPool.h"
#include <span>
#include <utility>
#include <vector>

struct Object;

class Physics {
public:
    void Solve(std::span<Object> objects, SpatialGrid& grid, float hw, float hh, float dt);

private:
    void ResolveCollisions(std::span<Object> objects, SpatialGrid& grid);

    static void ResolveBoundaries(std::span<Object> objects, float hw, float hh);
    static void ResolveCell(std::span<Object> objects, SpatialGrid& grid, int col, int row);

    const int  threadCount_{ (int)std::max(1u, std::thread::hardware_concurrency()) };
    ThreadPool pool_{ threadCount_ };
    std::vector<std::pair<int,int>> colorCells_;
};
