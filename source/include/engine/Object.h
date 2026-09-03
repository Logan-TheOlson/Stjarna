#pragma once
#include "Renderable.h"
#include "util/Vector.h"

struct App;

struct Object {
    Vec2 pos;
    Vec2 vel = Vec2(0, 0);
    Vec2 acc = Vec2(0,0);  // net acceleration — reset every Config::Physics::ForceInterval substeps, not every integrate (see Engine.cpp's main loop)
    Renderable renderable;
    float density = 0.f;
    float pressure = 0.f;

    float Radius() const;
    void  Draw(App& app) const;
};

