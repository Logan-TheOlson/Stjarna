#pragma once

struct Object;

struct CollisionEngine {
    float halfW, halfH;
    void ResolveBoundary(Object& obj) const;
};
