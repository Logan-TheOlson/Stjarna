# Stjarna

A barebones 2D simulation engine built on Vulkan + SDL3. Designed to be a minimal, modular foundation — the engine handles windowing, rendering, timing, and integration; you fill in `Init()` and `Update()`.

## Architecture

```
main.cpp      — your simulation: Init() and Update()
Engine.cpp    — run loop, integration, object management (internals)
Physics.cpp   — forces, boundaries, collision resolution
VulkanContext — instanced GPU rendering of circles and rectangles
```

### Frame order

```
Init()                    called once at startup

each frame:
  Update(dt)              your code: apply forces, resolve collisions, manage grid
  Integrate(dt)           engine: substep velocity/position update, resets accelerations
  Draw + RenderFrame      engine: GPU submission
```

## User API

Include `Engine.h` and `Physics.h` in `main.cpp`.

### Object management (`Engine.h`)

```cpp
extern std::vector<Object> objects;
extern SpatialGrid         grid;

Object& CreateObject(float x, float y, Circle shape);
Object& CreateObject(float x, float y, Rectangle shape);
void    RemoveObject(size_t i);          // O(1) swap-with-last

float   ScreenHalfWidth();
float   ScreenHalfHeight();
```

### Physics (`Physics.h`)

```cpp
Physics::ApplyGravity(objects);                              // adds Config::Physics::Gravity to ay
Physics::ApplyForce(obj, fx, fy);                           // adds to ax/ay
Physics::ResolveBoundaries(objects, hw, hh);                // clamp + reflect velocity
Physics::ResolveCollisions(objects, grid);                  // impulse resolution via spatial grid
```

### Shapes

```cpp
Circle    { float radius; Color color; }
Rectangle { float halfW, halfH; Color color; }
Color     { float r, g, b, a; }        // 0–1
```

### Typical Update

```cpp
void Update(float dt) {
    Physics::ApplyGravity(objects);
    Physics::ResolveBoundaries(objects, ScreenHalfWidth(), ScreenHalfHeight());

    grid.Clear();
    for (int i = 0; i < (int)objects.size(); i++)
        grid.Insert(i, objects[i].x, objects[i].y);
    Physics::ResolveCollisions(objects, grid);
}
```

## Configuration (`Config.h`)

```cpp
Config::WindowWidth / WindowHeight
Config::Physics::Gravity        // pixels/s^2
Config::Physics::Restitution    // bounce coefficient
Config::Physics::Friction
Config::Physics::Substeps       // integration substeps per frame
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
