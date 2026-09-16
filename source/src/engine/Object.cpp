#include "engine/Object.h"
#include "renderer/App.h"

float Object::Radius() const {
    return renderable.geometry.radius;
}

void Object::Draw(App& app, const Color* colorOverride, const float* xOverride) const {
    if (renderable.shader == Shader::Circle)
        app.AddCircle(xOverride ? *xOverride : pos.x, pos.y, renderable.geometry.radius, colorOverride ? *colorOverride : renderable.color);
}
