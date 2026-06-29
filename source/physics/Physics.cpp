#include "Physics.h"
#include "../Object.h"
#include "../Config.h"
#include <algorithm>
#include <cmath>
#include <thread>

void Physics::Solve(std::span<Object> objects, SpatialGrid& grid, float hw, float hh, float dt) {
    const float subDt = dt / Config::Physics::Substeps;
    const int   n     = (int)objects.size();

    for (int step = 0; step < Config::Physics::Substeps; step++) {
        Integrate(objects, subDt);

        grid.Clear();
        for (int i = 0; i < n; i++)
            grid.Insert(i, objects[i].x, objects[i].y);

        ResolveCollisions(objects, grid);
        ResolveBoundaries(objects, hw, hh);
    }
}

void Physics::Integrate(std::span<Object> objects, float subDt) {
    const float subDamp = std::pow(Config::Physics::Damping, 1.0f / Config::Physics::Substeps);

    for (auto& b : objects) {
        b.vy -= Config::Physics::Gravity * subDt;
        b.vx *= subDamp;
        b.vy *= subDamp;
        if (std::abs(b.vx) < Config::Physics::SleepThreshold) b.vx = 0.0f;
        if (std::abs(b.vy) < Config::Physics::SleepThreshold) b.vy = 0.0f;
        b.x += b.vx * subDt;
        b.y += b.vy * subDt;
    }
}

void Physics::ResolveBoundaries(std::span<Object> objects, float hw, float hh) {
    for (auto& b : objects) {
        const float r = b.shape.radius;
        if (b.x - r < -hw) { b.x = -hw + r; if (b.vx < 0.0f) b.vx *= -Config::Physics::Restitution; }
        if (b.x + r >  hw) { b.x =  hw - r; if (b.vx > 0.0f) b.vx *= -Config::Physics::Restitution; }
        if (b.y - r < -hh) { b.y = -hh + r; if (b.vy < 0.0f) b.vy *= -Config::Physics::Restitution; }
        if (b.y + r >  hh) { b.y =  hh - r; if (b.vy > 0.0f) b.vy *= -Config::Physics::Restitution; }
    }
}

void Physics::ResolveCell(std::span<Object> objects, SpatialGrid& grid, int col, int row) {
    for (int i : grid.GetCell(col, row)) {
        Object& a = objects[i];
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                for (int j : grid.GetCell(col + dc, row + dr)) {
                    if (j <= i) continue;
                    Object& b = objects[j];

                    float dx = b.x - a.x, dy = b.y - a.y;
                    float distSq  = dx*dx + dy*dy;
                    float minDist = a.shape.radius + b.shape.radius;
                    if (distSq >= minDist*minDist || distSq < 1e-8f) continue;

                    float dist   = std::sqrt(distSq);
                    float invD   = 1.0f / dist;
                    float nx     = dx * invD, ny = dy * invD;

                    float half = (minDist - dist) * 0.5f;
                    a.x -= nx * half; a.y -= ny * half;
                    b.x += nx * half; b.y += ny * half;

                    float dvx = b.vx - a.vx, dvy = b.vy - a.vy;
                    float dvn = dvx*nx + dvy*ny;
                    if (dvn >= 0.0f) continue;

                    float Jn = -(1.0f + Config::Physics::Restitution) * dvn * 0.5f;
                    a.vx -= Jn * nx; a.vy -= Jn * ny;
                    b.vx += Jn * nx; b.vy += Jn * ny;

                    float tx = dvx - dvn * nx;
                    float ty = dvy - dvn * ny;
                    float tvSq = tx*tx + ty*ty;
                    if (tvSq < 1e-8f) continue;

                    float tvLen = std::sqrt(tvSq);
                    float invTv = 1.0f / tvLen;
                    tx *= invTv; ty *= invTv;

                    float Jt = std::min(tvLen * 0.5f, Config::Physics::Friction * Jn);
                    a.vx += Jt * tx; a.vy += Jt * ty;
                    b.vx -= Jt * tx; b.vy -= Jt * ty;
                }
            }
        }
    }
}

void Physics::ResolveCollisions(std::span<Object> objects, SpatialGrid& grid) {
    const int cols = grid.Cols(), rows = grid.Rows();

    for (int pr = 0; pr < 3; pr++) {
        for (int pc = 0; pc < 3; pc++) {
            colorCells_.clear();
            for (int row = pr; row < rows; row += 3)
                for (int col = pc; col < cols; col += 3)
                    if (!grid.GetCell(col, row).empty())
                        colorCells_.push_back({ col, row });

            if (colorCells_.empty()) continue;

            const int n     = (int)colorCells_.size();
            const int tasks = std::min(threadCount_, n);
            for (int t = 0; t < tasks; t++) {
                const int start = t * n / tasks;
                const int end   = (t + 1) * n / tasks;
                pool_.Submit([&objects, &grid, &cc = colorCells_, start, end] {
                    for (int k = start; k < end; k++)
                        ResolveCell(objects, grid, cc[k].first, cc[k].second);
                });
            }
            pool_.Wait();
        }
    }
}
