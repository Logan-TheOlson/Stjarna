#pragma once
#include "Circle.h"

struct App;

struct Object {
    float  x, y;
    float  vx = 0.0f, vy = 0.0f;
    Circle shape;

    void Draw(App& app) const;
};
