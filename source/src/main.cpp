#include "Engine.h"
#include "physics/Physics.h"
#include "Config.h"

void Init() {
    constexpr float radius  = Config::Defaults::CircleRadius;
    constexpr float spacing = radius * 10.0f;
    constexpr float startX  = -(4 * spacing / 2.0f);
    constexpr float startY  = -(4 * spacing / 2.0f);
    for (int x = 0; x < 5; x++)
        for (int y = 0; y < 5; y++)
            CreateObject(startX + static_cast<float>(x) * spacing,
                         startY + static_cast<float>(y) * spacing,
                         Circle{ radius, { 0.2f, 0.6f, 1.0f, 1.0f } });
}

void Update(float) {
    Physics::ResolveBoundaries(objects, ScreenHalfWidth(), ScreenHalfHeight());
}
