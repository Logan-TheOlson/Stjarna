#include "engine/Engine.h"
#include "engine/Scene.h"
#include "renderer/App.h"
#include "util/Profiler.h"
#include "Config.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// User-defined entry points
void Init();
void Update(float dt);

std::vector<Object> objects;

// Directory the running executable lives in, so output files land next to it regardless of the
// process's current working directory.
static std::filesystem::path ExecutableDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return std::filesystem::path(path, path + len).parent_path();
#endif
    return std::filesystem::current_path();
}

// One kinetic-energy sample: `run` distinguishes separate Start-Simulation/Restart sessions
// within the same process, so a discontinuity from a reset doesn't read as an instability event
// when the CSV is plotted.
struct KESample { int run; int frame; float ke; };

// Appends run,frame,kineticEnergy rows collected over the process's lifetime to a CSV next to the
// executable, for offline comparison (e.g. plotting energy stability across runs in Python).
static void WriteKineticEnergyCsv(const std::vector<KESample>& keByFrame) {
    const auto outPath = ExecutableDir() / "kinetic_energy.csv";
    std::ofstream out(outPath);
    if (!out) return;

    out << "run,frame,kinetic_energy\n";
    for (const auto& s : keByFrame)
        out << s.run << ',' << s.frame << ',' << s.ke << '\n';
}

static App      app;
static Profiler profiler;
static bool     profilerOpen = false;

// Simulation only advances in Running; Menu leaves `objects` untouched (cleared) and skips
// physics entirely so the menu screen never pays SPH cost.
enum class AppState { Menu, Running };
static AppState appState = AppState::Menu;

struct MenuConfig {
    bool  record = false;
    char  title[128] = "capture";
    int   fps = 60;
    float lengthSeconds = 10.f;
    int   sceneIndex = 0;
};
static MenuConfig menuConfig;

// Identifies which Start-Simulation/Restart session a KESample belongs to (see WriteKineticEnergyCsv).
static int runIndex = -1;

static void ResetSimulation() {
    objects.clear();
    Init();
    runIndex++;
}

static void StartSimulationFromMenu() {
    LoadScene(ScenePresets[menuConfig.sceneIndex]);
    ResetSimulation();
    if (menuConfig.record)
        app.StartRecording(menuConfig.title, menuConfig.fps, menuConfig.lengthSeconds);
    appState = AppState::Running;
}

// Resets the sim in place without leaving Running (and without touching any active recording) —
// distinct from ReturnToMenu, which is the "stop and reconfigure" path.
static void RestartSimulation() {
    ResetSimulation();
}

static void ReturnToMenu() {
    app.StopRecording();
    objects.clear();
    appState = AppState::Menu;
}

static void DrawMenu() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Stjarna", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::TextUnformatted("SPH Fluid Simulation");
    ImGui::Separator();

    const Scene& selectedScene = ScenePresets[menuConfig.sceneIndex];
    if (ImGui::BeginCombo("Scene", selectedScene.name)) {
        for (int i = 0; i < static_cast<int>(ScenePresets.size()); i++) {
            const bool isSelected = (i == menuConfig.sceneIndex);
            if (ImGui::Selectable(ScenePresets[i].name, isSelected))
                menuConfig.sceneIndex = i;
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", selectedScene.description);

    ImGui::Separator();
    ImGui::Checkbox("Record to video", &menuConfig.record);
    if (menuConfig.record) {
        ImGui::InputText("Title", menuConfig.title, sizeof(menuConfig.title));
        ImGui::InputInt("FPS", &menuConfig.fps);
        menuConfig.fps = std::clamp(menuConfig.fps, 1, 240);
        ImGui::InputFloat("Length (s, 0 = unlimited)", &menuConfig.lengthSeconds, 1.0f, 10.0f, "%.0f");
        menuConfig.lengthSeconds = std::max(menuConfig.lengthSeconds, 0.f);
        ImGui::TextDisabled("Saved to recordings/<title>.mp4 (requires ffmpeg on PATH)");
    }

    ImGui::Separator();
    if (ImGui::Button("Start Simulation", ImVec2(260.f, 0.f)))
        StartSimulationFromMenu();

    ImGui::End();
}

static void DrawRunningOverlay() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.f, 10.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("Controls", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

    ImGui::TextDisabled("%s", ActiveScene.name);

    if (app.IsRecording())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC %.1fs", app.RecordedSeconds());

    if (ImGui::Button("Restart")) RestartSimulation();
    ImGui::SameLine();
    if (ImGui::Button("Menu")) ReturnToMenu();

    ImGui::End();
}

Object& CreateObject(float x, float y, Renderable r) {
    objects.push_back({ .pos = Vec2(x, y), .renderable = r });
    return objects.back();
}

void RemoveObject(size_t i) {
    objects[i] = objects.back();
    objects.pop_back();
}

// The simulation container's half-extents — the active scene's boundary fractions applied to the
// actual window size, so a scene can request walls closer in than the window edge.
float ScreenHalfWidth()  { return app.HalfWidth()  * ActiveScene.boundary.widthFrac; }
float ScreenHalfHeight() { return app.HalfHeight() * ActiveScene.boundary.heightFrac; }

// Draws a black frame at the active scene's boundary, so a container narrower than the window
// (see SceneBoundary) reads as a wall instead of an invisible line partway across the screen.
// AddRectOutline's border band is drawn inward from the half-extent passed in, so the rect is
// inflated by BorderThickness here — that puts the band entirely outside the wall (half-extent
// stays exactly where particles clamp) instead of eating into the fluid's play area.
static void DrawBoundary(App& app) {
    constexpr float BorderThickness = 6.0f;
    constexpr Color BorderColor     = { 0.f, 0.f, 0.f, 1.f };
    app.AddRectOutline(0.f, 0.f, ScreenHalfWidth() + BorderThickness, ScreenHalfHeight() + BorderThickness,
                        BorderThickness, BorderColor);
}

// vt is the velocity component tangential to this axis's wall (e.g. vel.y when resolving the
// x-axis walls); when noSlipWalls is enabled it gets damped on contact instead of passing
// through untouched, so wall-adjacent particles drag against the boundary rather than free-slip.
static void resolveAxis(float& p, float& v, float& vt, float half, float r) {
    const auto& physics = ActiveScene.physics;
    bool hit = false;
    if (p - r < -half) { p = -half + r; if (v < 0.0f) { v *= -physics.restitution; hit = true; } }
    if (p + r >  half) { p =  half - r; if (v > 0.0f) { v *= -physics.restitution; hit = true; } }
    if (hit && physics.noSlipWalls) vt *= 1.0f - physics.friction;
}

// Integrates a single substep, reusing whatever force (particle.acc) the last Update() call
// computed — held constant across ActiveScene.physics.forceInterval substeps at a time to trade
// some staleness back for compute cost. acc is reset only when it's about to be recomputed
// (see the main loop), not here.
static void Integrate(float subDt) {
    const float hw = ScreenHalfWidth(), hh = ScreenHalfHeight();
    for (auto& obj : objects) {
        const float r = obj.Radius();
        obj.vel += obj.acc * subDt;
        obj.pos += obj.vel * subDt;
        resolveAxis(obj.pos.x, obj.vel.x, obj.vel.y, hw, r);
        resolveAxis(obj.pos.y, obj.vel.y, obj.vel.x, hh, r);
    }
}

int main(int, char**) {
    app.Init(Config::WindowTitle, Config::WindowWidth, Config::WindowHeight);
    app.SetUICallback([]() {
        if (appState == AppState::Menu) DrawMenu();
        else                            DrawRunningOverlay();
    });

    objects.reserve(100000);

    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();

    std::vector<KESample> keByFrame;
    keByFrame.reserve(1u << 16); // avoid mid-run reallocations from skewing frame timing
    int lastRunIndex = runIndex, runFrame = 0;

    profiler.MarkFrameStart();
    while (app.PollEvents()) {
        if (app.TakeF1Toggle()) {
            profilerOpen = !profilerOpen;
            app.SetProfilerOpen(profilerOpen);
        }

        auto  now = Clock::now();
        float dt  = std::min(std::chrono::duration<float>(now - last).count(), 1.0f / 30.0f);
        last      = now;

        if (appState == AppState::Running) {
            const auto& physics = ActiveScene.physics;
            // Force is recomputed every forceInterval substeps (not once per frame, not every
            // substep) — see Integrate()'s comment for why a stale force is a real energy-gain
            // source, and why recomputing every substep was too expensive at this particle count.
            const float subDt = dt / static_cast<float>(physics.substeps);
            for (int step = 0; step < physics.substeps; step++) {
                if (step % physics.forceInterval == 0) {
                    for (auto& obj : objects) obj.acc = Vec2(0.0f, 0.0f);
                    Update(subDt);
                }
                Integrate(subDt);
            }
        }
        profiler.MarkComputeEnd();

        for (auto& obj : objects)
            obj.Draw(app);
        if (appState == AppState::Running) DrawBoundary(app);

        app.RenderFrame(dt);
        profiler.MarkRenderEnd();
        app.SetObjectCount(static_cast<int>(objects.size()));

        float kineticEnergy = 0.f;
        for (auto& obj : objects)
            kineticEnergy += 0.5f * (obj.vel.x * obj.vel.x + obj.vel.y * obj.vel.y);
        app.SetKineticEnergy(kineticEnergy);
        if (appState == AppState::Running) {
            if (runIndex != lastRunIndex) { lastRunIndex = runIndex; runFrame = 0; }
            keByFrame.push_back({ runIndex, runFrame++, kineticEnergy });
        }

        if (profiler.Tick())
            app.SetProfilerStats(profiler.GetStats());

        profiler.MarkFrameStart();
    }
    WriteKineticEnergyCsv(keByFrame);
    app.Shutdown();
    return 0;
}
