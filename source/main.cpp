#include "app/App.h"
#include "Circle.h"

App app;

Circle ball;
float vy = 0;

void Init() {
    ball = Circle(0, 0, 10, { 1.0f, 0.5f, 0.1f, 1.0f });
}

void Update() {
    vy -= 0.01f;
    ball.y += vy;
    ball.Draw();
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