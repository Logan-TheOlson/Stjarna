#include "engine/Engine.h"
#include "engine/Scene.h"
#include "renderer/App.h"
#include "util/DataRecorder.h"
#include "util/Filename.h"
#include "util/Profiler.h"
#include "Config.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

// Bins particles by y-position across the current scene's channel height and writes each bin's
// average x-velocity to a CSV next to the executable — e.g. the parabolic profile a Poiseuille
// scene should develop. Overwritten every time this is called (see the main loop's call site),
// so the file always reflects a recent snapshot rather than one moment early in the transient.
static void WriteVelocityProfileCsv() {
    constexpr int kBins = 40;
    const float hh = ScreenHalfHeight();
    if (hh <= 0.f || objects.empty()) return;

    std::array<double, kBins> sumVx{};
    std::array<int, kBins>    count{};
    for (auto& obj : objects) {
        const float t   = (obj.pos.y + hh) / (2.f * hh); // 0..1 across the channel
        const int   bin = std::clamp(static_cast<int>(t * kBins), 0, kBins - 1);
        sumVx[bin] += obj.vel.x;
        count[bin]++;
    }

    const auto outPath = ExecutableDir() / "velocity_profile.csv";
    std::ofstream out(outPath);
    if (!out) return;

    out << "y,avg_vx,count\n";
    const float binHeight = 2.f * hh / kBins;
    for (int b = 0; b < kBins; b++) {
        if (count[b] == 0) continue;
        const float yCenter = -hh + (b + 0.5f) * binHeight;
        out << yCenter << ',' << (sumVx[b] / count[b]) << ',' << count[b] << '\n';
    }
}

static App      app;
static Profiler profiler;
static bool     profilerOpen = false;

// Simulation only advances in Running/Rendering; Menu leaves `objects` untouched (cleared) and
// skips physics entirely so the menu screen never pays SPH cost. Rendering runs the same physics
// as Running, but crunched back-to-back at a fixed 1/fps dt and always drawn+captured straight to
// video — no real-time pacing, and no live playback afterward (see AdvanceRendering/
// DrawRenderingOverlay).
enum class AppState { Menu, Running, Rendering };
static AppState appState = AppState::Menu;

// Freezes physics (rendering, recording, and CSV data-saving all keep running) — toggled by
// Space or the Pause/Resume button. Independent of appState so a paused sim stays paused across
// UI redraws; only Restart/Menu/StartSimulation touch it.
static bool paused = false;

// Periodic-channel scenes (Poiseuille) drift the whole particle cloud sideways as the imposed
// force advects it, which makes the pressure-driven profile (fast center, slow walls) hard to
// read — it's buried under the bulk translation. When enabled, the drawn (not simulated) x
// position is offset by the accumulated mean flow velocity each substep (see Integrate), so the
// cloud appears stationary on average and only the relative motion between fast/slow bands shows.
static bool  cameraFollow  = true;
static float cameraOffsetX = 0.f;

constexpr float StepDt         = 1.f / 60.f; // fixed nominal frame length for manual stepping, so a
                                              // step is deterministic regardless of real elapsed time
constexpr int   BigStepFrames  = 10;         // frames advanced by Left Arrow / the "Step x10" button

struct MenuConfig {
    bool  record = false;
    char  title[128] = "capture";
    int   fps = 60;
    float lengthSeconds = 10.f;
    // Only meaningful alongside `record`: renders+captures every frame back-to-back at 1/fps
    // instead of pacing to real time, so the video finishes in however long that takes to compute
    // rather than lengthSeconds of real time — see AdvanceRendering.
    bool  batchRender = false;
    int   sceneIndex = 0;

    // Particle grid override, applied via BuildScene() instead of the selected preset's own
    // count — synced from the preset's default whenever sceneIndex changes (see DrawMenu), so
    // switching scenes doesn't carry over an unrelated count left over from a previous selection.
    int   gridCountX = 0;
    int   gridCountY = 0;
    bool  circularSpawn = false; // see the same comment: synced from the preset on scene change

    bool  saveData = false;
    char  dataTitle[128] = "data";
    int   dataRate = 30; // samples/sec, independent of render fps — see the "Save data to CSV" section
    float dataLengthSeconds = 10.f;
    bool  dataPosition = true;
    bool  dataVelocity = true;
    bool  dataSpeed = false;
    bool  dataDensity = true;
    bool  dataPressure = true;
};
static MenuConfig menuConfig;

// Owned directly by Engine.cpp (unlike Recorder, which lives behind App/VulkanContext because it
// needs the composited GPU frame) since the data it samples — `objects` — already lives here.
static DataRecorder dataRecorder;
static float        dataAccum{ 0.f };        // seconds since the last sample, gated at menuConfig.dataRate like Recorder's fps gate
static float        dataElapsed{ 0.f };       // sim seconds since this data save started, for the CSV's time column
static uint32_t     dataSavedFrames{ 0 };
static int          dataRateActive{ 0 };
static float        dataLengthActive{ 0.f };  // 0 = unlimited, mirrors menuConfig.lengthSeconds for video

// Identifies which Start-Simulation/Restart session a KESample belongs to (see WriteKineticEnergyCsv).
static int runIndex = -1;

// Accumulated across the process's lifetime and written once at shutdown (see main()) — file
// scope (not a main()-local) so DrawAndSubmitFrame can push to it from both Running's per-real-
// frame call site and AdvanceRendering's per-video-frame one.
static std::vector<KESample> keByFrame;
static int lastRunIndexForKE = -1;
static int runFrameForKE     = 0;

// Live coloring: recolors every particle by a current simulation field instead of its scene
// color, so a fast flow (e.g. Poiseuille) is readable even when it's moving too fast to track
// individual particles. Each field gets its own low/high color pair, independently editable, so
// switching modes is visually obvious rather than "the same gradient, different numbers".
enum class ColorMode { Off, Speed, Pressure, Density };
static const char* ColorModeNames[] = { "Off (scene color)", "Speed", "Pressure", "Density" };

struct ColorRamp { Color low; Color high; };
static ColorMode colorMode = ColorMode::Off;
// Indexed by ColorMode; entry 0 (Off) is unused.
static ColorRamp colorRamps[] = {
    {},
    { {0.15f, 0.25f, 0.95f, 1.f}, {0.95f, 0.15f, 0.10f, 1.f} }, // Speed:    blue   -> red
    { {0.05f, 0.55f, 0.15f, 1.f}, {1.00f, 0.90f, 0.10f, 1.f} }, // Pressure: green  -> yellow
    { {0.55f, 0.05f, 0.65f, 1.f}, {1.00f, 0.55f, 0.05f, 1.f} }, // Density:  purple -> orange
};

static float FieldValue(const Object& o, ColorMode mode) {
    switch (mode) {
        case ColorMode::Speed:    return magnitude(o.vel);
        case ColorMode::Pressure: return o.pressure;
        case ColorMode::Density:  return o.density;
        default:                  return 0.f;
    }
}

static Color LerpColor(const Color& a, const Color& b, float t) {
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f };
}

// The mode + gradient picker that decides how DrawAndSubmitFrame colors particles — shared by
// DrawRunningOverlay (the live corner panel) and DrawMenu's Record section, so a batch render
// (which never visits Running) still gets a chance to set this before the video starts, not just
// whatever colorMode happened to be left over from a previous live session.
static void DrawColorByControls(float contentWidth) {
    const float swatchSize = ImGui::GetFrameHeight();

    ImGui::TextUnformatted("Color by");
    ImGui::SetNextItemWidth(contentWidth);
    int modeIdx = static_cast<int>(colorMode);
    if (ImGui::Combo("##colorby", &modeIdx, ColorModeNames, IM_ARRAYSIZE(ColorModeNames)))
        colorMode = static_cast<ColorMode>(modeIdx);

    if (colorMode != ColorMode::Off) {
        ColorRamp& ramp = colorRamps[modeIdx];
        ImGui::Spacing();

        // Label on the left, swatch pinned to the content's right edge — NoInputs leaves just the
        // swatch button here, all sliders live in the popup it opens.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Low");
        ImGui::SameLine(contentWidth - swatchSize);
        ImGui::ColorEdit3("##low", &ramp.low.r, ImGuiColorEditFlags_NoInputs);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("High");
        ImGui::SameLine(contentWidth - swatchSize);
        ImGui::ColorEdit3("##high", &ramp.high.r, ImGuiColorEditFlags_NoInputs);
    }
}

// Defined further down (needs ScreenHalfWidth/Height); forward-declared so AdvanceSimulation can
// call it despite living earlier in the file, next to the other pause/step/menu logic.
static void Integrate(float subDt);

static void ResetSimulation() {
    objects.clear();
    Init();
    runIndex++;
    paused = false;
    cameraOffsetX = 0.f;
}

// Advances one substep loop's worth of physics by dt — factored out of the main loop so manual
// stepping (paused, one keypress/button = one frame) runs the exact same code path as normal
// per-frame advancement, just with a fixed dt instead of the frame's real one.
static void AdvanceSimulation(float dt) {
    const auto& physics = ActiveScene.physics;
    // Force is recomputed every forceInterval substeps (not once per frame, not every substep) —
    // see Integrate()'s comment for why a stale force is a real energy-gain source, and why
    // recomputing every substep was too expensive at this particle count.
    const float subDt = dt / static_cast<float>(physics.substeps);
    for (int step = 0; step < physics.substeps; step++) {
        if (step % physics.forceInterval == 0) {
            for (auto& obj : objects) obj.acc = Vec2(0.0f, 0.0f);
            Update(subDt);
        }
        Integrate(subDt);
    }
}

static void TogglePause() { paused = !paused; }

// Stepping always pauses first (if not already) — pressing Step/the arrow keys while running
// freezes the sim on that frame rather than requiring a separate pause press first.
static void StepOnce() { paused = true; AdvanceSimulation(StepDt); }
static void StepBig()  { paused = true; for (int i = 0; i < BigStepFrames; i++) AdvanceSimulation(StepDt); }

static void StartSimulationFromMenu() {
    Scene scene = BuildScene(menuConfig.sceneIndex, menuConfig.gridCountX, menuConfig.gridCountY);
    scene.spawn.circular = menuConfig.circularSpawn;
    LoadScene(scene);
    ResetSimulation();
    bool recording = false;
    if (menuConfig.record)
        recording = app.StartRecording(menuConfig.title, menuConfig.fps, menuConfig.lengthSeconds);
    if (menuConfig.saveData) {
        DataRecorder::Fields fields;
        fields.position = menuConfig.dataPosition;
        fields.velocity = menuConfig.dataVelocity;
        fields.speed    = menuConfig.dataSpeed;
        fields.density  = menuConfig.dataDensity;
        fields.pressure = menuConfig.dataPressure;

        const auto  dir = ExecutableDir() / "data";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::string safeTitle = SanitizeFilename(menuConfig.dataTitle, "data");
        if (!ec && dataRecorder.Start((dir / (safeTitle + ".csv")).string(), fields)) {
            dataRateActive   = std::clamp(menuConfig.dataRate, 1, 240);
            dataLengthActive = menuConfig.dataLengthSeconds;
            dataAccum        = 0.f;
            dataElapsed      = 0.f;
            dataSavedFrames  = 0;
        }
    }

    // batchRender only makes sense with an active recording to drive it — if StartRecording()
    // failed (e.g. ffmpeg missing), fall back to Running rather than entering a Rendering state
    // that would just find app.IsRecording() false immediately and bounce straight back to Menu.
    if (recording && menuConfig.batchRender) {
        // AdvanceRendering captures every video frame itself via RenderOffscreenFrame; the
        // per-tick RenderFrame() call (see main()) is only there to keep the progress overlay on
        // screen and must not also feed the recorder — see SetCaptureFromSwapchain.
        app.SetCaptureFromSwapchain(false);
        appState = AppState::Rendering;
    } else {
        appState = AppState::Running;
    }
}

// Resets the sim in place without leaving Running (and without touching any active recording or
// data save) — distinct from ReturnToMenu, which is the "stop and reconfigure" path.
static void RestartSimulation() {
    ResetSimulation();
}

static void ReturnToMenu() {
    app.StopRecording();
    dataRecorder.Stop();
    objects.clear();
    // Restore the default so the next live recording (Running, not batchRender) captures normally
    // — only batch rendering ever turns this off.
    app.SetCaptureFromSwapchain(true);
    appState = AppState::Menu;
}

static void DrawMenu() {
    // Every free-text row below (TextDisabled/TextUnformatted, including this scene's own
    // varying-length description) wraps at the same fixed width every stretchy input/button also
    // uses — computed as whichever Checkbox label in this window is actually the widest, since a
    // Checkbox can't wrap. Sizing everything else to match that (rather than a guessed constant,
    // which was narrower than "Render as fast as possible (no live playback)") is what stops the
    // width-260 rows from sitting stuck against the left edge with dead space to their right once
    // that wider Checkbox forces the window itself wider.
    auto CheckboxWidth = [](const char* label) {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
    };
    const float MenuContentWidth = std::max({
        260.f,
        CheckboxWidth("Record to video"),
        CheckboxWidth("Render as fast as possible (no live playback)"),
        CheckboxWidth("Circular spawn"),
        CheckboxWidth("Save data to CSV"),
    });
    // Width for an input whose own trailing label (e.g. InputInt's "Grid X") renders past the
    // box — reserves that label's space out of MenuContentWidth so box+label together land
    // exactly on the wrap boundary above instead of a few pixels past it, which otherwise wraps
    // the label onto its own line (PushTextWrapPos affects every Text-family draw, including a
    // widget's own trailing label).
    auto LabeledItemWidth = [&](const char* label) {
        return MenuContentWidth - ImGui::CalcTextSize(label).x - ImGui::GetStyle().ItemInnerSpacing.x;
    };

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Stjarna", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + MenuContentWidth);

    ImGui::TextUnformatted("SPH Fluid Simulation");
    ImGui::Separator();

    ImGui::TextUnformatted("Scene");
    ImGui::SetNextItemWidth(MenuContentWidth);
    if (ImGui::BeginCombo("##scene", ScenePresets[menuConfig.sceneIndex].name)) {
        for (int i = 0; i < static_cast<int>(ScenePresets.size()); i++) {
            const bool isSelected = (i == menuConfig.sceneIndex);
            if (ImGui::Selectable(ScenePresets[i].name, isSelected))
                menuConfig.sceneIndex = i;
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    // Bound only after the combo above has had a chance to change sceneIndex this same frame —
    // a reference bound before that point would keep aliasing the *previous* selection for the
    // rest of this function (references don't rebind when the index changes later), which used
    // to make the grid-count reset below copy the old scene's spawn instead of the new one.
    const Scene& selectedScene = ScenePresets[menuConfig.sceneIndex];
    ImGui::TextUnformatted(selectedScene.description);

    // Reset to the newly-selected preset's own count on every scene change (including the very
    // first DrawMenu call, since lastSyncedSceneIndex starts at a value no real sceneIndex can
    // equal) rather than carrying over whatever count an unrelated previous scene left behind.
    static int lastSyncedSceneIndex = -1;
    if (menuConfig.sceneIndex != lastSyncedSceneIndex) {
        menuConfig.gridCountX    = selectedScene.spawn.gridCountX;
        menuConfig.gridCountY    = selectedScene.spawn.gridCountY;
        menuConfig.circularSpawn = selectedScene.spawn.circular;
        lastSyncedSceneIndex     = menuConfig.sceneIndex;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Particles");
    ImGui::SetNextItemWidth(LabeledItemWidth("Grid X"));
    ImGui::InputInt("Grid X", &menuConfig.gridCountX);
    ImGui::SetNextItemWidth(LabeledItemWidth("Grid Y"));
    ImGui::InputInt("Grid Y", &menuConfig.gridCountY);
    menuConfig.gridCountX = std::max(menuConfig.gridCountX, 1);
    menuConfig.gridCountY = std::max(menuConfig.gridCountY, 1);
    ImGui::Checkbox("Circular spawn", &menuConfig.circularSpawn);
    ImGui::TextDisabled("%d particles", menuConfig.gridCountX * menuConfig.gridCountY);
    ImGui::TextDisabled("Real-Time Limit: %d particles",
                         selectedScene.realTimeLimitX * selectedScene.realTimeLimitY);

    ImGui::Separator();
    ImGui::Checkbox("Record to video", &menuConfig.record);
    if (menuConfig.record) {
        ImGui::SetNextItemWidth(LabeledItemWidth("Title"));
        ImGui::InputText("Title", menuConfig.title, sizeof(menuConfig.title));
        ImGui::SetNextItemWidth(LabeledItemWidth("FPS"));
        ImGui::InputInt("FPS", &menuConfig.fps);
        menuConfig.fps = std::clamp(menuConfig.fps, 1, 240);
        ImGui::SetNextItemWidth(LabeledItemWidth("Length (s, 0 = unlimited)"));
        ImGui::InputFloat("Length (s, 0 = unlimited)", &menuConfig.lengthSeconds, 1.0f, 10.0f, "%.0f");
        menuConfig.lengthSeconds = std::max(menuConfig.lengthSeconds, 0.f);
        ImGui::TextDisabled("Saved to recordings/<title>.mp4 (requires ffmpeg on PATH)");

        ImGui::Checkbox("Render as fast as possible (no live playback)", &menuConfig.batchRender);
        if (menuConfig.batchRender)
            ImGui::TextDisabled("Computes and saves the video directly at FPS/Length above, taking\n"
                                 "however long that takes to compute instead of that many real\n"
                                 "seconds. Cancel button (or Space) stops it early.");

        // Batch-rendered video never visits Running's live "Controls" panel, so without this a
        // batch render could only ever use the scene's plain color — set it here instead, before
        // the video starts. Applies to a live recording too, just redundant with the panel there.
        ImGui::Spacing();
        DrawColorByControls(MenuContentWidth);
    }

    ImGui::Separator();
    ImGui::Checkbox("Save data to CSV", &menuConfig.saveData);
    if (menuConfig.saveData) {
        ImGui::SetNextItemWidth(LabeledItemWidth("Data title"));
        ImGui::InputText("Data title", menuConfig.dataTitle, sizeof(menuConfig.dataTitle));
        ImGui::SetNextItemWidth(LabeledItemWidth("Sample rate (Hz)"));
        ImGui::InputInt("Sample rate (Hz)", &menuConfig.dataRate);
        menuConfig.dataRate = std::clamp(menuConfig.dataRate, 1, 240);
        ImGui::SetNextItemWidth(LabeledItemWidth("Save length (s, 0 = unlimited)"));
        ImGui::InputFloat("Save length (s, 0 = unlimited)", &menuConfig.dataLengthSeconds, 1.0f, 10.0f, "%.0f");
        menuConfig.dataLengthSeconds = std::max(menuConfig.dataLengthSeconds, 0.f);

        ImGui::TextUnformatted("Fields");
        ImGui::Checkbox("Position", &menuConfig.dataPosition);
        ImGui::SameLine();
        ImGui::Checkbox("Velocity", &menuConfig.dataVelocity);
        ImGui::SameLine();
        ImGui::Checkbox("Speed", &menuConfig.dataSpeed);
        ImGui::Checkbox("Density", &menuConfig.dataDensity);
        ImGui::SameLine();
        ImGui::Checkbox("Pressure", &menuConfig.dataPressure);

        ImGui::TextDisabled("Saved to data/<title>.csv (one row per particle per sample)");
    }

    ImGui::Separator();
    if (ImGui::Button("Start Simulation", ImVec2(MenuContentWidth, 0.f)))
        StartSimulationFromMenu();

    ImGui::PopTextWrapPos();
    ImGui::End();
}

// Safe to draw now that VulkanContext::RenderFrame captures the video frame in a first pass and
// only draws ImGui in a second one on top — this overlay lands in that second pass, so unlike
// before it never reaches the recorded video no matter how long it stays on screen.
static void DrawRenderingOverlay() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("Rendering", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::TextUnformatted(ActiveScene.name);
    const float recorded = app.RecordedSeconds();
    if (menuConfig.lengthSeconds > 0.f) {
        ImGui::Text("%.1f / %.1f seconds rendered", recorded, menuConfig.lengthSeconds);
        const float frac = std::clamp(recorded / menuConfig.lengthSeconds, 0.f, 1.f);
        ImGui::ProgressBar(frac, ImVec2(260.f, 0.f));
    } else {
        ImGui::Text("%.1f seconds rendered (unlimited length)", recorded);
    }
    ImGui::TextDisabled("Rendering as fast as possible, straight to video —\nno live playback follows.");

    if (ImGui::Button("Cancel", ImVec2(260.f, 0.f))) ReturnToMenu();

    ImGui::End();
}

static void DrawRunningOverlay() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.f, 10.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Controls", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

    // Fixed content width (rather than whatever the widest row happens to auto-size to) so the
    // panel doesn't visibly resize when the color-by rows appear/disappear, and so the row-content
    // computations below (button/combo alignment) have a stable width to work from.
    constexpr float ContentWidth = 240.f;

    ImGui::TextUnformatted(ActiveScene.name);
    if (app.IsRecording()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC %.1fs", app.RecordedSeconds());
    }
    if (dataRecorder.IsActive()) {
        ImGui::SameLine();
        const float dataSeconds = dataRateActive > 0 ? static_cast<float>(dataSavedFrames) / dataRateActive : 0.f;
        ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "DATA %.1fs", dataSeconds);
    }
    if (paused) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "PAUSED");
    }

    ImGui::Spacing();
    const float buttonWidth = (ContentWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Restart", ImVec2(buttonWidth, 0.f))) RestartSimulation();
    ImGui::SameLine();
    if (ImGui::Button("Menu", ImVec2(buttonWidth, 0.f))) ReturnToMenu();

    ImGui::Spacing();
    if (ImGui::Button(paused ? "Resume" : "Pause", ImVec2(buttonWidth, 0.f))) TogglePause();
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(buttonWidth, 0.f))) StepOnce();
    if (ImGui::Button("Step x10", ImVec2(ContentWidth, 0.f))) StepBig();
    ImGui::TextDisabled("Space to pause, arrows to step");

    if (ActiveScene.boundary.periodicX) {
        ImGui::Spacing();
        ImGui::Checkbox("Follow flow (camera)", &cameraFollow);
        ImGui::TextDisabled("Cancels bulk drift so the profile's relative\nmotion reads as motion instead of scrolling past");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawColorByControls(ContentWidth);

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
// recording's own resolution while one is active (so the boundary/wall-bounce geometry can't
// drift out from under a captured video frame that's pinned to a fixed size — see
// VulkanContext::RecordingHalfWidth/Height), or the live window size otherwise.
float ScreenHalfWidth()  { return app.RecordingHalfWidth()  * ActiveScene.boundary.widthFrac; }
float ScreenHalfHeight() { return app.RecordingHalfHeight() * ActiveScene.boundary.heightFrac; }

// Draws a black frame at the active scene's boundary, so a container narrower than the window
// (see SceneBoundary) reads as a wall instead of an invisible line partway across the screen.
// AddRectOutline's border band is drawn inward from the half-extent passed in, so the rect is
// inflated by BorderThickness here — that puts the band entirely outside the wall (half-extent
// stays exactly where particles clamp) instead of eating into the fluid's play area.
static void DrawBoundary(App& app) {
    constexpr float BorderThickness = 6.0f;
    constexpr Color BorderColor     = { 0.f, 0.f, 0.f, 1.f };
    const float hw = ScreenHalfWidth(), hh = ScreenHalfHeight();

    if (ActiveScene.boundary.periodicX) {
        // No side walls to draw when X wraps — just solid top/bottom bars marking the channel's
        // Y confinement. A border thickness bigger than the bar itself fills it solid rather than
        // drawing it as a hollow frame (see AddRectOutline).
        constexpr float FillSolid = 1e6f;
        const float barHalfHeight = BorderThickness * 0.5f;
        app.AddRectOutline(0.f,  hh + barHalfHeight, hw + BorderThickness, barHalfHeight, FillSolid, BorderColor);
        app.AddRectOutline(0.f, -hh - barHalfHeight, hw + BorderThickness, barHalfHeight, FillSolid, BorderColor);
    } else {
        app.AddRectOutline(0.f, 0.f, hw + BorderThickness, hh + BorderThickness, BorderThickness, BorderColor);
    }
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

// Wraps x into (-hw, hw], the same range periodicX teleporting keeps simulated positions in —
// used to fold the camera-follow display offset back into range regardless of how far it's
// accumulated, rather than only handling a single wrap like the simulated-position teleport does.
static float WrapX(float x, float hw) {
    const float span = 2.f * hw;
    if (span <= 0.f) return x;
    x = std::fmod(x + hw, span);
    if (x < 0.f) x += span;
    return x - hw;
}

// Integrates a single substep, reusing whatever force (particle.acc) the last Update() call
// computed — held constant across ActiveScene.physics.forceInterval substeps at a time to trade
// some staleness back for compute cost. acc is reset only when it's about to be recomputed
// (see the main loop), not here.
static void Integrate(float subDt) {
    const float hw = ScreenHalfWidth(), hh = ScreenHalfHeight();
    const bool  periodicX = ActiveScene.boundary.periodicX;
    double sumVx = 0.0; // mean flow velocity this substep, for the camera-follow display offset
    for (auto& obj : objects) {
        const float r = obj.Radius();
        obj.vel += obj.acc * subDt;
        obj.pos += obj.vel * subDt;
        if (periodicX) {
            // Teleport across the seam rather than bounce — velocity is left untouched, so the
            // channel behaves like an infinite domain rather than a wall.
            if      (obj.pos.x >  hw) obj.pos.x -= 2.f * hw;
            else if (obj.pos.x < -hw) obj.pos.x += 2.f * hw;
            sumVx += obj.vel.x;
        } else {
            resolveAxis(obj.pos.x, obj.vel.x, obj.vel.y, hw, r);
        }
        resolveAxis(obj.pos.y, obj.vel.y, obj.vel.x, hh, r);
    }
    if (periodicX && cameraFollow && !objects.empty()) {
        cameraOffsetX += static_cast<float>(sumVx / objects.size()) * subDt;
        cameraOffsetX  = WrapX(cameraOffsetX, hw);
    }
}

// Draws the current particles/boundary and submits the frame (capturing it if a video recording
// is active), then records the per-frame bookkeeping (kinetic-energy history, CSV data sample) —
// one call is exactly one simulated+rendered(+captured) frame, at whatever dt physics was just
// advanced by. Running calls this once per real frame; AdvanceRendering calls it back-to-back at
// a fixed 1/fps dt instead, with no pacing to real time and no ImGui overlay drawn over it (an
// overlay would get baked straight into the captured video — see VulkanContext::RenderFrame).
static void DrawAndSubmitFrame(float dt, bool offscreen) {
    const bool coloring = colorMode != ColorMode::Off && !objects.empty();
    float kineticEnergy = 0.f;
    float colorLo = 0.f, colorHi = 0.f;
    if (coloring) colorLo = colorHi = FieldValue(objects[0], colorMode);
    for (auto& obj : objects) {
        kineticEnergy += 0.5f * (obj.vel.x * obj.vel.x + obj.vel.y * obj.vel.y);
        if (coloring) {
            const float v = FieldValue(obj, colorMode);
            colorLo = std::min(colorLo, v);
            colorHi = std::max(colorHi, v);
        }
    }

    // Display-only x shift (see Integrate's cameraOffsetX accumulation) — never touches obj.pos,
    // so physics/CSV/data-save all still see true simulated positions.
    const bool  followCam = ActiveScene.boundary.periodicX && cameraFollow;
    const float hw        = ScreenHalfWidth();
    if (coloring) {
        // Auto-scaled to the current frame's range rather than a fixed scale — the field's
        // magnitude varies wildly across scenes (and over a Poiseuille run's own ramp-up), so a
        // fixed range would either clip everything to one end or wash out early on.
        const ColorRamp& ramp = colorRamps[static_cast<int>(colorMode)];
        const float range = colorHi - colorLo;
        for (auto& obj : objects) {
            float t = range > 0.f ? (FieldValue(obj, colorMode) - colorLo) / range : 0.f;
            Color c = LerpColor(ramp.low, ramp.high, t);
            if (followCam) {
                float dispX = WrapX(obj.pos.x - cameraOffsetX, hw);
                obj.Draw(app, &c, &dispX);
            } else {
                obj.Draw(app, &c);
            }
        }
    } else {
        for (auto& obj : objects) {
            if (followCam) {
                float dispX = WrapX(obj.pos.x - cameraOffsetX, hw);
                obj.Draw(app, nullptr, &dispX);
            } else {
                obj.Draw(app);
            }
        }
    }
    DrawBoundary(app);

    // Batch rendering (see AdvanceRendering) never touches the swapchain/window at all — offscreen
    // draws+captures every frame unconditionally instead, so it can't be throttled by vsync or by
    // the OS deprioritizing an occluded/backgrounded window.
    if (offscreen) app.RenderOffscreenFrame();
    else           app.RenderFrame(dt);
    app.SetObjectCount(static_cast<int>(objects.size()));
    app.SetKineticEnergy(kineticEnergy);

    if (runIndex != lastRunIndexForKE) { lastRunIndexForKE = runIndex; runFrameForKE = 0; }
    keByFrame.push_back({ runIndex, runFrameForKE++, kineticEnergy });

    if (dataRecorder.IsActive()) {
        dataAccum += dt;
        const float interval = 1.f / static_cast<float>(dataRateActive);
        // Sampled on its own accumulator, independent of the render/physics rate, same as
        // Recorder's fps gate for video — dataRate is usually far below the sim's own rate.
        if (dataAccum >= interval) {
            dataAccum -= interval;

            std::vector<ParticleSample> samples;
            samples.reserve(objects.size());
            for (auto& obj : objects)
                samples.push_back({ obj.pos.x, obj.pos.y, obj.vel.x, obj.vel.y,
                                     magnitude(obj.vel), obj.density, obj.pressure });

            dataRecorder.SubmitFrame(static_cast<int>(dataSavedFrames), dataElapsed, std::move(samples));
            dataSavedFrames++;
            if (dataLengthActive > 0.f && dataSavedFrames >= static_cast<uint32_t>(dataLengthActive * dataRateActive))
                dataRecorder.Stop();
        }
        dataElapsed += dt;
    }
}

// Crunches physics AND rendering/capture back-to-back at a fixed 1/fps dt instead of pacing to
// real time, so a video finishes as fast as the CPU/GPU can produce it rather than taking
// lengthSeconds of real time. Draws+captures offscreen (see DrawAndSubmitFrame/
// RenderOffscreenFrame) — no swapchain/window involvement, so nothing here can be throttled by
// vsync or by the OS deprioritizing an occluded/backgrounded window. Relies on the recorder's own
// length cap to know when it's done — once app.IsRecording() goes false (length reached, or
// Cancel's StopRecording()), there's no live playback to fall into, so this goes straight back to
// Menu. Still budgeted to a wall-clock slice per call so the main loop's PollEvents() keeps the
// window responsive during a long render instead of hanging; the caller separately refreshes the
// on-screen progress overlay once per real tick (a plain, cheap swapchain present, decoupled from
// this loop's own pace) rather than this loop touching the swapchain itself.
static void AdvanceRendering() {
    using Clock = std::chrono::steady_clock;
    const auto  budgetStart            = Clock::now();
    constexpr float FrameBudgetSeconds = 1.f / 30.f;
    const float frameDt = 1.f / static_cast<float>(std::max(menuConfig.fps, 1));

    while (app.IsRecording()) {
        AdvanceSimulation(frameDt);
        DrawAndSubmitFrame(frameDt, /*offscreen=*/true);
        if (std::chrono::duration<float>(Clock::now() - budgetStart).count() >= FrameBudgetSeconds)
            break;
    }

    if (!app.IsRecording()) ReturnToMenu(); // StopRecording() here is a no-op, already stopped itself
}

int main(int, char**) {
    app.Init(Config::WindowTitle, Config::WindowWidth, Config::WindowHeight);
    app.SetUICallback([]() {
        switch (appState) {
            case AppState::Menu:      DrawMenu(); break;
            case AppState::Running:   DrawRunningOverlay(); break;
            case AppState::Rendering: DrawRenderingOverlay(); break;
        }
    });

    objects.reserve(100000);
    keByFrame.reserve(1u << 16); // avoid mid-run reallocations from skewing frame timing

    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();

    profiler.MarkFrameStart();
    while (app.PollEvents()) {
        if (app.TakeF1Toggle()) {
            profilerOpen = !profilerOpen;
            app.SetProfilerOpen(profilerOpen);
        }
        // Drained every frame regardless of appState so a press queued during the menu (or while
        // a text field had focus) never leaks into the next Running session.
        const bool stepPressed    = app.TakeStepForward();
        const bool bigStepPressed = app.TakeStepForwardBig();
        const bool spacePressed   = app.TakeSpaceToggle();

        auto  now = Clock::now();
        float dt  = std::min(std::chrono::duration<float>(now - last).count(), 1.0f / 30.0f);
        last      = now;

        if (appState == AppState::Running) {
            if (spacePressed)   TogglePause();
            if (stepPressed)    StepOnce();
            if (bigStepPressed) StepBig();
            if (!paused) AdvanceSimulation(dt);
            profiler.MarkComputeEnd();
            DrawAndSubmitFrame(dt, /*offscreen=*/false);
            profiler.MarkRenderEnd();
        } else if (appState == AppState::Rendering) {
            // Same key Running uses for pause/resume; the Cancel button in DrawRenderingOverlay
            // does the same thing.
            if (spacePressed) {
                ReturnToMenu();
            } else {
                profiler.MarkComputeEnd();
                AdvanceRendering();
                profiler.MarkRenderEnd();
            }
            // Refreshes the progress overlay (or, if AdvanceRendering just finished, the menu) —
            // deliberately separate from AdvanceRendering's own offscreen capture loop above, so
            // showing it only costs one swapchain acquire/present per real tick, not one per video
            // frame captured within that tick's budget.
            app.RenderFrame(dt);
        } else {
            app.RenderFrame(dt); // Menu: just the UI, no particles/bookkeeping
            profiler.MarkComputeEnd();
            profiler.MarkRenderEnd();
        }

        if (profiler.Tick()) {
            app.SetProfilerStats(profiler.GetStats());
            if (appState == AppState::Running) WriteVelocityProfileCsv();
        }

        profiler.MarkFrameStart();
    }
    WriteKineticEnergyCsv(keByFrame);
    app.Shutdown();
    return 0;
}
