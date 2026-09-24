# Stjarna

A barebones 2D simulation engine built on Vulkan + SDL3. Designed to be a minimal, modular foundation — the engine handles windowing, rendering, timing, and integration

## Architecture

```
main.cpp      — your simulation: Init() and Update()
Engine.cpp    — run loop, integration, object management (internals)
Object.cpp    — per-object radius/draw
Vector.h/cpp  — Vec2 math (util/)
VulkanContext — instanced GPU rendering of circles
```

### Frame order

```
Init()                    called once at startup

each frame:
  Update(dt)              your code: apply forces, resolve boundaries, etc.
  Integrate(dt)           engine: substep velocity/position update, resets acceleration
  Draw + RenderFrame      engine: GPU submission
```

## User API

Include `Engine.h` in `main.cpp`.

### Object management (`Engine.h`)

```cpp
extern std::vector<Object> objects;

Object& CreateObject(float x, float y, Renderable r);
void    RemoveObject(size_t i);          // O(1) swap-with-last

float   ScreenHalfWidth();
float   ScreenHalfHeight();
```

### Object (`Object.h`)

```cpp
struct Object {
    Vec2       pos;
    Vec2       vel = Vec2(0, 0);
    Vec2       acc = Vec2(0, 0);   // net acceleration — reset after each Integrate
    Renderable renderable;

    float Radius() const;
};
```

Apply forces in `Update()` by accumulating into `obj.acc` — the engine integrates and clears it for you every frame.

### Vec2 (`util/Vector.h`)

```cpp
struct Vec2 { float x, y; };

Vec2 operator+(Vec2, Vec2);
Vec2 operator-(Vec2, Vec2);
Vec2 operator*(Vec2, float);   // and float * Vec2
// += -= *= also available

float distance(Vec2 a, Vec2 b);
float magnitude(Vec2 a);
```

### Shapes / Renderable (`Renderable.h`)

```cpp
Circle     { float radius; }
Shader     { Circle }                  // which GPU pipeline draws this object
Renderable { Color color; Shader shader; Circle geometry; }
Color      { float r, g, b, a; }        // 0–1
```

### Typical Update

```cpp
void Update(float dt) {
    for (auto& b : objects) {
        const float r = b.Radius();
        // clamp + reflect off screen edges, per axis
        if (b.pos.x - r < -hw) { b.pos.x = -hw + r; if (b.vel.x < 0.0f) b.vel.x *= -ActiveScene.physics.restitution; }
        // ...
    }
}
```

## Configuration (`Config.h` / `Scene.h`)

`Config.h` only holds window setup:

```cpp
Config::WindowTitle / WindowWidth / WindowHeight
```

Everything tunable per-run — boundaries, spawn layout, gravity, and the physics/particle constants
below — lives in `Scene.h`/`Scene.cpp` as runtime scene presets, read off `ActiveScene`:

```cpp
ActiveScene.physics.restitution     // bounce coefficient
ActiveScene.physics.friction
ActiveScene.physics.substeps        // integration substeps per frame
ActiveScene.particles.smoothingRadius  // SPH kernel smoothing radius
```

## Dependencies

- **Vulkan SDK** — set `VULKAN_SDK` environment variable
- **SDL3** — expected in `$VULKAN_SDK/lib`
- **ImGui** — fetched automatically via CMake FetchContent

## Build

```bash
cd source
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

Press **F1** in-app to toggle the performance overlay (fps, frame/compute/render ms, object count).
