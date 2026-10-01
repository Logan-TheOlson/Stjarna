#include "engine/ScenePresetIO.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>

namespace {

std::string FormatFloat(float v) {
    // %.9g round-trips a float exactly without std::to_string's trailing-zero noise.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}
float ParseFloat(const std::string& s, float fallback) {
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return (end && end != s.c_str()) ? v : fallback;
}
int ParseInt(const std::string& s, int fallback) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    return (end && end != s.c_str()) ? static_cast<int>(v) : fallback;
}
std::string FormatBool(bool v) { return v ? "true" : "false"; }
bool ParseBool(const std::string& s, bool fallback) {
    if (s == "true")  return true;
    if (s == "false") return false;
    return fallback;
}
std::string GravityMethodToString(GravityMethod m) {
    return m == GravityMethod::BarnesHut ? "BarnesHut" : "Genuine2D";
}
GravityMethod GravityMethodFromString(const std::string& s, GravityMethod fallback) {
    if (s == "BarnesHut") return GravityMethod::BarnesHut;
    if (s == "Genuine2D") return GravityMethod::Genuine2D;
    return fallback;
}
std::string EosModelToString(EosModel e) {
    return e == EosModel::WCSPH ? "WCSPH" : "Polytropic";
}
EosModel EosModelFromString(const std::string& s, EosModel fallback) {
    if (s == "WCSPH")      return EosModel::WCSPH;
    if (s == "Polytropic") return EosModel::Polytropic;
    return fallback;
}
std::string ViscosityModelToString(ViscosityModel m) {
    return m == ViscosityModel::Morris ? "Morris" : "Monaghan";
}
ViscosityModel ViscosityModelFromString(const std::string& s, ViscosityModel fallback) {
    if (s == "Monaghan") return ViscosityModel::Monaghan;
    if (s == "Morris")   return ViscosityModel::Morris;
    return fallback;
}
// Preset files are one field per line ("key=value"); a literal newline in a user-typed
// name/description would corrupt that, so it's flattened to a space on save. Not otherwise
// escaped (e.g. an '=' in a description) — these are short, hand-typed labels, not adversarial
// input, so a plain flat format is enough.
std::string SanitizeForLine(const std::string& s) {
    std::string out = s;
    std::replace(out.begin(), out.end(), '\n', ' ');
    return out;
}

// One Scene field per entry, driving both directions from the same table so save/load can't
// drift out of sync by editing only one of two separately-written switch statements.
struct PresetField {
    const char*                                     key;
    std::function<std::string(const Scene&)>        get;
    std::function<void(Scene&, const std::string&)> set;
};

const std::vector<PresetField>& Fields() {
    static const std::vector<PresetField> fields = {
        { "name",        [](const Scene& s) { return SanitizeForLine(s.name); },
                          [](Scene& s, const std::string& v) { s.name = v; } },
        { "description", [](const Scene& s) { return SanitizeForLine(s.description); },
                          [](Scene& s, const std::string& v) { s.description = v; } },

        { "boundary.widthFrac",  [](const Scene& s) { return FormatFloat(s.boundary.widthFrac); },
                                  [](Scene& s, const std::string& v) { s.boundary.widthFrac = ParseFloat(v, s.boundary.widthFrac); } },
        { "boundary.heightFrac", [](const Scene& s) { return FormatFloat(s.boundary.heightFrac); },
                                  [](Scene& s, const std::string& v) { s.boundary.heightFrac = ParseFloat(v, s.boundary.heightFrac); } },
        { "boundary.halfWidthPx",  [](const Scene& s) { return FormatFloat(s.boundary.halfWidthPx); },
                                    [](Scene& s, const std::string& v) { s.boundary.halfWidthPx = ParseFloat(v, s.boundary.halfWidthPx); } },
        { "boundary.halfHeightPx", [](const Scene& s) { return FormatFloat(s.boundary.halfHeightPx); },
                                    [](Scene& s, const std::string& v) { s.boundary.halfHeightPx = ParseFloat(v, s.boundary.halfHeightPx); } },
        { "boundary.periodicX",  [](const Scene& s) { return FormatBool(s.boundary.periodicX); },
                                  [](Scene& s, const std::string& v) { s.boundary.periodicX = ParseBool(v, s.boundary.periodicX); } },

        { "physics.force.x",       [](const Scene& s) { return FormatFloat(s.physics.force.x); },
                                    [](Scene& s, const std::string& v) { s.physics.force.x = ParseFloat(v, s.physics.force.x); } },
        { "physics.force.y",       [](const Scene& s) { return FormatFloat(s.physics.force.y); },
                                    [](Scene& s, const std::string& v) { s.physics.force.y = ParseFloat(v, s.physics.force.y); } },
        { "physics.restitution",   [](const Scene& s) { return FormatFloat(s.physics.restitution); },
                                    [](Scene& s, const std::string& v) { s.physics.restitution = ParseFloat(v, s.physics.restitution); } },
        { "physics.friction",      [](const Scene& s) { return FormatFloat(s.physics.friction); },
                                    [](Scene& s, const std::string& v) { s.physics.friction = ParseFloat(v, s.physics.friction); } },
        { "physics.noSlipWalls",   [](const Scene& s) { return FormatBool(s.physics.noSlipWalls); },
                                    [](Scene& s, const std::string& v) { s.physics.noSlipWalls = ParseBool(v, s.physics.noSlipWalls); } },
        { "physics.substeps",      [](const Scene& s) { return std::to_string(s.physics.substeps); },
                                    [](Scene& s, const std::string& v) { s.physics.substeps = ParseInt(v, s.physics.substeps); } },
        { "physics.forceInterval", [](const Scene& s) { return std::to_string(s.physics.forceInterval); },
                                    [](Scene& s, const std::string& v) { s.physics.forceInterval = ParseInt(v, s.physics.forceInterval); } },

        { "gravity.enabled",          [](const Scene& s) { return FormatBool(s.gravity.enabled); },
                                       [](Scene& s, const std::string& v) { s.gravity.enabled = ParseBool(v, s.gravity.enabled); } },
        { "gravity.method",           [](const Scene& s) { return GravityMethodToString(s.gravity.method); },
                                       [](Scene& s, const std::string& v) { s.gravity.method = GravityMethodFromString(v, s.gravity.method); } },
        { "gravity.g",                [](const Scene& s) { return FormatFloat(s.gravity.g); },
                                       [](Scene& s, const std::string& v) { s.gravity.g = ParseFloat(v, s.gravity.g); } },
        { "gravity.particleMass",     [](const Scene& s) { return FormatFloat(s.gravity.particleMass); },
                                       [](Scene& s, const std::string& v) { s.gravity.particleMass = ParseFloat(v, s.gravity.particleMass); } },
        { "gravity.softening",        [](const Scene& s) { return FormatFloat(s.gravity.softening); },
                                       [](Scene& s, const std::string& v) { s.gravity.softening = ParseFloat(v, s.gravity.softening); } },
        { "gravity.macTheta",         [](const Scene& s) { return FormatFloat(s.gravity.macTheta); },
                                       [](Scene& s, const std::string& v) { s.gravity.macTheta = ParseFloat(v, s.gravity.macTheta); } },
        { "gravity.maxLeafParticles", [](const Scene& s) { return std::to_string(s.gravity.maxLeafParticles); },
                                       [](Scene& s, const std::string& v) { s.gravity.maxLeafParticles = ParseInt(v, s.gravity.maxLeafParticles); } },
        { "gravity.validate",         [](const Scene& s) { return FormatBool(s.gravity.validate); },
                                       [](Scene& s, const std::string& v) { s.gravity.validate = ParseBool(v, s.gravity.validate); } },

        { "particles.circleRadius",    [](const Scene& s) { return FormatFloat(s.particles.circleRadius); },
                                        [](Scene& s, const std::string& v) { s.particles.circleRadius = ParseFloat(v, s.particles.circleRadius); } },
        { "particles.circleColor.r",   [](const Scene& s) { return FormatFloat(s.particles.circleColor.r); },
                                        [](Scene& s, const std::string& v) { s.particles.circleColor.r = ParseFloat(v, s.particles.circleColor.r); } },
        { "particles.circleColor.g",   [](const Scene& s) { return FormatFloat(s.particles.circleColor.g); },
                                        [](Scene& s, const std::string& v) { s.particles.circleColor.g = ParseFloat(v, s.particles.circleColor.g); } },
        { "particles.circleColor.b",   [](const Scene& s) { return FormatFloat(s.particles.circleColor.b); },
                                        [](Scene& s, const std::string& v) { s.particles.circleColor.b = ParseFloat(v, s.particles.circleColor.b); } },
        { "particles.circleColor.a",   [](const Scene& s) { return FormatFloat(s.particles.circleColor.a); },
                                        [](Scene& s, const std::string& v) { s.particles.circleColor.a = ParseFloat(v, s.particles.circleColor.a); } },
        { "particles.smoothingRadius", [](const Scene& s) { return FormatFloat(s.particles.smoothingRadius); },
                                        [](Scene& s, const std::string& v) { s.particles.smoothingRadius = ParseFloat(v, s.particles.smoothingRadius); } },
        { "particles.targetDensity",   [](const Scene& s) { return FormatFloat(s.particles.targetDensity); },
                                        [](Scene& s, const std::string& v) { s.particles.targetDensity = ParseFloat(v, s.particles.targetDensity); } },
        { "particles.stiffness",       [](const Scene& s) { return FormatFloat(s.particles.stiffness); },
                                        [](Scene& s, const std::string& v) { s.particles.stiffness = ParseFloat(v, s.particles.stiffness); } },
        { "particles.eos",             [](const Scene& s) { return EosModelToString(s.particles.eos); },
                                        [](Scene& s, const std::string& v) { s.particles.eos = EosModelFromString(v, s.particles.eos); } },
        { "particles.exponent",        [](const Scene& s) { return std::to_string(s.particles.exponent); },
                                        [](Scene& s, const std::string& v) { s.particles.exponent = ParseInt(v, s.particles.exponent); } },
        { "particles.polytropicIndex", [](const Scene& s) { return FormatFloat(s.particles.polytropicIndex); },
                                        [](Scene& s, const std::string& v) { s.particles.polytropicIndex = ParseFloat(v, s.particles.polytropicIndex); } },
        { "particles.viscosity",          [](const Scene& s) { return FormatFloat(s.particles.viscosity); },
                                           [](Scene& s, const std::string& v) { s.particles.viscosity = ParseFloat(v, s.particles.viscosity); } },
        { "particles.viscosityQuadratic", [](const Scene& s) { return FormatFloat(s.particles.viscosityQuadratic); },
                                           [](Scene& s, const std::string& v) { s.particles.viscosityQuadratic = ParseFloat(v, s.particles.viscosityQuadratic); } },
        { "particles.viscosityModel",     [](const Scene& s) { return ViscosityModelToString(s.particles.viscosityModel); },
                                           [](Scene& s, const std::string& v) { s.particles.viscosityModel = ViscosityModelFromString(v, s.particles.viscosityModel); } },
        { "particles.kinematicViscosity", [](const Scene& s) { return FormatFloat(s.particles.kinematicViscosity); },
                                           [](Scene& s, const std::string& v) { s.particles.kinematicViscosity = ParseFloat(v, s.particles.kinematicViscosity); } },

        { "spawn.gridCountX",  [](const Scene& s) { return std::to_string(s.spawn.gridCountX); },
                                [](Scene& s, const std::string& v) { s.spawn.gridCountX = ParseInt(v, s.spawn.gridCountX); } },
        { "spawn.gridCountY",  [](const Scene& s) { return std::to_string(s.spawn.gridCountY); },
                                [](Scene& s, const std::string& v) { s.spawn.gridCountY = ParseInt(v, s.spawn.gridCountY); } },
        { "spawn.offsetXFrac", [](const Scene& s) { return FormatFloat(s.spawn.offsetXFrac); },
                                [](Scene& s, const std::string& v) { s.spawn.offsetXFrac = ParseFloat(v, s.spawn.offsetXFrac); } },
        { "spawn.circular",    [](const Scene& s) { return FormatBool(s.spawn.circular); },
                                [](Scene& s, const std::string& v) { s.spawn.circular = ParseBool(v, s.spawn.circular); } },
    };
    return fields;
}

} // namespace

bool SaveScenePreset(const std::filesystem::path& path, const Scene& scene) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const auto& field : Fields())
        out << field.key << '=' << field.get(scene) << '\n';
    return true;
}

bool LoadScenePreset(const std::filesystem::path& path, Scene& out) {
    std::ifstream in(path);
    if (!in) return false;

    const auto& fields = Fields();
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        const auto it = std::find_if(fields.begin(), fields.end(),
            [&](const PresetField& f) { return key == f.key; });
        if (it != fields.end())
            it->set(out, value);
        else
            std::cerr << "LoadScenePreset: unknown key '" << key << "' in " << path << ", skipping\n";
    }
    return true;
}

std::vector<ScenePresetEntry> ScanUserPresets(const std::filesystem::path& presetsDir) {
    std::vector<ScenePresetEntry> entries;
    std::error_code ec;
    if (!std::filesystem::exists(presetsDir, ec) || ec) return entries;

    for (const auto& file : std::filesystem::directory_iterator(presetsDir, ec)) {
        if (ec) break;
        if (file.path().extension() != ".scenepreset") continue;

        Scene scene;
        if (!LoadScenePreset(file.path(), scene)) {
            std::cerr << "ScanUserPresets: couldn't open " << file.path() << "\n";
            continue;
        }
        entries.push_back({ scene.name, scene.description, file.path() });
    }

    std::sort(entries.begin(), entries.end(),
        [](const ScenePresetEntry& a, const ScenePresetEntry& b) { return a.name < b.name; });
    return entries;
}

Scene BuildSceneFromPreset(const ScenePresetEntry& entry, int gridCountX, int gridCountY) {
    Scene s;
    LoadScenePreset(entry.path, s);
    s.spawn.gridCountX = std::max(gridCountX, 1);
    s.spawn.gridCountY = std::max(gridCountY, 1);
    return s;
}
