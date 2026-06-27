#include "app/App.h"
#include "Circle.h"
#include "physics/collision_Engine.h"
#include "Config.h"

App app;

Circle ball;

void Init() {
    ball = Circle(Config::Defaults::CirclePos.x, Config::Defaults::CirclePos.y,
                  Config::Defaults::CircleRadius, Config::Defaults::CircleColor);
}

void Update() {

    ball.vy -= 0.25f;
    ball.x  += ball.vx;
    ball.y  += ball.vy;

    CollisionEngine::ResolveBoundary(ball, app.HalfWidth(), app.HalfHeight());

    ball.Draw(app);
}

int main(int, char**) {
    app.Init(Config::WindowTitle, Config::WindowWidth, Config::WindowHeight);
    Init();
    while (app.PollEvents()) {
        Update();
        app.RenderFrame();
    }
    app.Shutdown();
    return 0;
}