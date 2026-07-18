#pragma once
#include "Renderable.h"

struct App;

struct Object {
    float      x, y;
    float      vx = 0.0f, vy = 0.0f;
    float      ax = 0.0f, ay = 0.0f;  // net acceleration — reset after each integrate
    Renderable renderable;

    float Radius() const;
    void  Draw(App& app) const;
};
