#pragma once
#include "engine/Scene.h"
#include <filesystem>
#include <string>
#include <vector>

// User-saved scene presets, stored as flat "key=value" text files next to the executable (no
// JSON/INI library anywhere in this codebase, and none added here). Distinct from the 3 compiled
// ScenePresets (Scene.h) — these are written by Engine.cpp's "Save Current as Preset" button and
// read back into the menu's scene list at startup and after each save.
struct ScenePresetEntry {
    std::string           name;
    std::string           description;
    std::filesystem::path path;
};

// Writes every Scene field to `path` as "key=value" lines. Overwrites unconditionally. Returns
// false only if the file can't be opened for writing.
bool SaveScenePreset(const std::filesystem::path& path, const Scene& scene);

// Reads a file written by SaveScenePreset back into `out`. A key not present in the file leaves
// `out`'s corresponding field untouched (forward-compatible with fields added to Scene later); an
// unrecognized key is skipped with a stderr warning. Returns false only if the file can't be
// opened at all.
bool LoadScenePreset(const std::filesystem::path& path, Scene& out);

// Scans `presetsDir` for *.scenepreset files, skipping (with a stderr warning) any that can't be
// loaded, sorted by name. Does not include the 3 compiled ScenePresets.
std::vector<ScenePresetEntry> ScanUserPresets(const std::filesystem::path& presetsDir);

// Loads entry.path and overrides its spawn grid to gridCountX x gridCountY (clamped to >= 1). No
// recalibration is attempted the way BuildScene() re-derives a compiled preset's physics
// constants for its own grid count — there's no generic formula for an arbitrary custom preset,
// so resizing one only changes how many particles spawn, not how they're tuned.
Scene BuildSceneFromPreset(const ScenePresetEntry& entry, int gridCountX, int gridCountY);
