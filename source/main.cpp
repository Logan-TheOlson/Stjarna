#include "app/App.h"

App app;

float cy = 0.0f;
float vy = 0.0f;

void Init() {
}

void Update() {
    vy -= 0.01f;
    cy += vy;
    app.AddCircle(0, cy, 100, { 1.0f, 0.5f, 0.1f, 1.0f });
}

int main(int, char**) {
    app.Init("Stjarna", 1920, 1080);
    Init();
    while (app.PollEvents()) {
        Update();
        app.RenderFrame();
    }
    app.Shutdown();
    return 0;
}
