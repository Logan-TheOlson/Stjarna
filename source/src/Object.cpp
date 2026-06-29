#include "Object.h"
#include "app/App.h"

void Object::Draw(App& app) const {
    std::visit([&](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, Circle>)
            app.AddCircle(x, y, s.radius, s.color);
        else if constexpr (std::is_same_v<T, Rectangle>)
            app.AddRectangle(x, y, s.halfW, s.halfH, s.color);
    }, shape);
}
