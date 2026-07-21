#pragma once
#include "Renderable.h"
#include "util/Vector.h"

struct App;

struct Object {
    Vec2 pos;
    Vec2 vel = Vec2(0, 0);
    Vec2 acc = Vec2(0,0);  // net acceleration — reset after each integrate
    Renderable renderable;

    float Radius() const;
    void  Draw(App& app) const;
};
