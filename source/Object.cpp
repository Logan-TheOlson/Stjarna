#include "Object.h"
#include "app/App.h"

void Object::Draw(App& app) const {
    app.AddCircle(x, y, shape.radius, shape.color);
}
