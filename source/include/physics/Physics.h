#pragma once
#include <span>

struct Object;

class Physics {
public:
    static void ApplyForce(Object& obj, float fx, float fy);

    static void ResolveBoundaries(std::span<Object> objects, float hw, float hh);
};
