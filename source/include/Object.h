#pragma once
#include "Circle.h"
#include "Rectangle.h"
#include <algorithm>
#include <variant>

struct App;

using Primitive = std::variant<Circle, Rectangle>;

struct Object {
    float     x, y;
    float     vx = 0.0f, vy = 0.0f;
    Primitive shape;

    float Radius() const;
    void  Draw(App& app) const;
};
