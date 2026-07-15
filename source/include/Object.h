#pragma once
#include "Circle.h"
#include "Rectangle.h"
#include <variant>

struct App;

using Primitive = std::variant<Circle, Rectangle>;

struct Object {
    float     x, y;
    float     vx = 0.0f, vy = 0.0f;
    float     ax = 0.0f, ay = 0.0f;  // net acceleration — reset after each integrate
    Primitive shape;

    float Radius() const;
    void  Draw(App& app) const;
};
