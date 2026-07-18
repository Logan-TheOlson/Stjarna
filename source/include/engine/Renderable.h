#pragma once
#include "Config.h"

struct Circle {
    float radius;
};

enum class Shader { Circle };

struct Renderable {
    Color  color;
    Shader shader;
    Circle geometry;
};
