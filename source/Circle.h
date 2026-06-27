#pragma once
#include "app/App.h"
#include "Config.h"

struct Circle {
    float x, y;
    float vx, vy;
    float radius;
    Color color;

    Circle() : x(Config::Defaults::CirclePos.x), y(Config::Defaults::CirclePos.y), vx(0), vy(0), radius(Config::Defaults::CircleRadius), color({ 1, 1, 1, 1 }) {}
    Circle(float x, float y, float radius, Color color)
        : x(x), y(y), vx(0), vy(0), radius(radius), color(color) {}

    void Draw(App& app) {
        app.AddCircle(x, y, radius, color);
    }
};