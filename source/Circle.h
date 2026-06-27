#pragma once
#include "app/App.h"
#include "Config.h"

extern App app;

struct Circle {
    float x, y;
    float radius;
    Color color;

    Circle() : x(Config::Defaults::CircleX), y(Config::Defaults::CircleY), radius(Config::Defaults::CircleRadius), color({ 1, 1, 1, 1 }) {}
    Circle(float x, float y, float radius, Color color)
        : x(x), y(y), radius(radius), color(color) {}

    void Draw() {
        app.AddCircle(x, y, radius, color);
    }
};