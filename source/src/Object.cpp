#include "Object.h"
#include "app/App.h"

float Object::Radius() const {
    return std::visit([](const auto& s) -> float {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, Circle>)
            return s.radius;
        else
            return std::max(s.halfW, s.halfH);
    }, shape);
}

void Object::Draw(App& app) const {
    std::visit([&](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, Circle>)
            app.AddCircle(x, y, s.radius, s.color);
        else if constexpr (std::is_same_v<T, Rectangle>)
            app.AddRectangle(x, y, s.halfW, s.halfH, s.color);
    }, shape);
}
