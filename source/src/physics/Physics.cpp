#include "physics/Physics.h"
#include "physics/SpatialGrid.h"
#include "Object.h"
#include "Config.h"
#include <algorithm>
#include <cmath>

void Physics::ApplyGravity(std::span<Object> objects) {
    for (auto& obj : objects)
        obj.ay -= Config::Physics::Gravity;
}

void Physics::ApplyForce(Object& obj, float fx, float fy) {
    obj.ax += fx;
    obj.ay += fy;
}

void Physics::ResolveBoundaries(std::span<Object> objects, float hw, float hh) {
    for (auto& b : objects) {
        const float r = b.Radius();
        if (b.x - r < -hw) { b.x = -hw + r; if (b.vx < 0.0f) b.vx *= -Config::Physics::Restitution; }
        if (b.x + r >  hw) { b.x =  hw - r; if (b.vx > 0.0f) b.vx *= -Config::Physics::Restitution; }
        if (b.y - r < -hh) { b.y = -hh + r; if (b.vy < 0.0f) b.vy *= -Config::Physics::Restitution; }
        if (b.y + r >  hh) { b.y =  hh - r; if (b.vy > 0.0f) b.vy *= -Config::Physics::Restitution; }
    }
}

void Physics::ResolveCollisions(std::span<Object> objects, const SpatialGrid& grid) {
    for (int row = 0; row < grid.Rows(); row++)
        for (int col = 0; col < grid.Cols(); col++)
            ResolveCell(objects, grid, col, row);
}

void Physics::ResolveCell(std::span<Object> objects, const SpatialGrid& grid, int col, int row) {
    for (int i : grid.GetCell(col, row)) {
        Object& a = objects[i];
        for (int dr = -1; dr <= 1; dr++) {
            for (int dc = -1; dc <= 1; dc++) {
                for (int j : grid.GetCell(col + dc, row + dr)) {
                    if (j <= i) continue;
                    Object& b = objects[j];

                    float dx = b.x - a.x, dy = b.y - a.y;
                    float distSq  = dx*dx + dy*dy;
                    float minDist = a.Radius() + b.Radius();
                    if (distSq >= minDist*minDist || distSq < 1e-8f) continue;

                    float dist = std::sqrt(distSq);
                    float nx   = dx / dist, ny = dy / dist;
                    float half = (minDist - dist) * 0.5f;
                    a.x -= nx * half; a.y -= ny * half;
                    b.x += nx * half; b.y += ny * half;

                    float dvx = b.vx - a.vx, dvy = b.vy - a.vy;
                    float dvn = dvx*nx + dvy*ny;
                    if (dvn >= 0.0f) continue;

                    float Jn = -(1.0f + Config::Physics::Restitution) * dvn * 0.5f;
                    a.vx -= Jn * nx; a.vy -= Jn * ny;
                    b.vx += Jn * nx; b.vy += Jn * ny;

                    float tx = dvx - dvn * nx, ty = dvy - dvn * ny;
                    float tvSq = tx*tx + ty*ty;
                    if (tvSq < 1e-8f) continue;

                    float tvLen = std::sqrt(tvSq);
                    tx /= tvLen; ty /= tvLen;
                    float Jt = std::min(tvLen * 0.5f, Config::Physics::Friction * Jn);
                    a.vx += Jt * tx; a.vy += Jt * ty;
                    b.vx -= Jt * tx; b.vy -= Jt * ty;
                }
            }
        }
    }
}
