#include "game/tuning_panel.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "core/mapgen.hpp"
#include "core/preset.hpp"
#include "core/terrain.hpp"

namespace pixeltd::game {
namespace {

constexpr const char* kCurveItems[] = {"linear", "smoothstep", "quintic"};
constexpr const char* kShapeItems[] = {"normal", "ridged", "billowed"};

// A preset name made safe as a file name: letters, digits, '-' and '_' only.
std::string sanitise(const char* name) {
    std::string out;
    for (const char* c = name; *c != '\0'; ++c) {
        const char ch = *c;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_') {
            out.push_back(ch);
        }
    }
    return out;
}

} // namespace

bool load_preset_file(const std::string& path, GenParams& out, std::string& errors) {
    std::size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        errors = std::string("can't read ") + path + ": " + SDL_GetError();
        return false;
    }
    const std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    GenParams p; // start from the defaults: the result is exactly the preset
    errors.clear();
    const bool ok = from_preset(text, p, errors);
    out = p;
    return ok;
}

MapStats measure(const Map& map, double gen_ms, double bake_ms) {
    MapStats s;
    s.attempts = map.stats.attempts;
    s.fallback = map.stats.fallback;
    s.gen_ms = gen_ms;
    s.bake_ms = bake_ms;
    int formation = 0;
    int spots = 0;
    int buildable = 0;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            formation += map.formation.at(x, y) != 0 ? 1 : 0;
            if (x % 2 == 0 && y % 2 == 0) {
                ++spots;
                buildable += placement_ok(map, SubCell{x * 2, y * 2}, kLightSub) ? 1 : 0;
            }
        }
    }
    const double cells = static_cast<double>(kMapW) * kMapH;
    s.formation_pct = 100.0 * formation / cells;
    s.buildable_pct = spots > 0 ? 100.0 * buildable / spots : 0.0;
    const Grid<int> dist = walk_distance_to_core(map);
    for (std::size_t i = 0; i < map.spawns.size() && i < s.spawn_walk.size(); ++i) {
        s.spawn_walk[i] = dist.at(map.spawns[i].cell);
    }
    return s;
}

TuningPanel::TuningPanel(std::string presets_dir) : presets_dir_(std::move(presets_dir)) {}

bool TuningPanel::slider(const char* label, int* v, int lo, int hi) {
    const bool changed = ImGui::SliderInt(label, v, lo, hi);
    preview_ = preview_ || changed;
    commit_ = commit_ || ImGui::IsItemDeactivatedAfterEdit();
    return changed;
}

void TuningPanel::noise_controls(const char* id, NoiseSpec& n) {
    ImGui::PushID(id);
    slider("octaves", &n.octaves, 1, kMaxOctaves);
    slider("step (cells)", &n.step, 2, kMapW / 2);
    if (n.octaves > 1) {
        slider("lacunarity %", &n.lacunarity_pct, 10, 100);
        slider("persistence %", &n.persistence_pct, 0, 100);
    }
    int curve = static_cast<int>(n.curve);
    if (ImGui::Combo("curve", &curve, kCurveItems, kCurves)) {
        n.curve = static_cast<Curve>(curve);
        commit_ = true;
    }
    int shape = static_cast<int>(n.shape);
    if (ImGui::Combo("shape", &shape, kShapeItems, kShapes)) {
        n.shape = static_cast<Shape>(shape);
        commit_ = true;
    }
    slider("warp (cells)", &n.warp_cells, 0, 64);
    if (n.warp_cells > 0) {
        slider("warp step", &n.warp_step, 2, kMapW / 2);
    }
    ImGui::PopID();
}

void TuningPanel::refresh_presets() {
    presets_.clear();
    int count = 0;
    char** names = SDL_GlobDirectory(presets_dir_.c_str(), "*.txt", 0, &count);
    if (names != nullptr) {
        for (int i = 0; i < count; ++i) {
            std::string name = names[i];
            if (name.size() > 4) {
                name.resize(name.size() - 4); // drop ".txt"
            }
            presets_.push_back(name);
        }
        SDL_free(static_cast<void*>(names));
    }
    std::sort(presets_.begin(), presets_.end());
    presets_listed_ = true;
}

TuningPanel::Change TuningPanel::draw(GenParams& params, std::uint64_t& seed, const MapStats& stats,
                                      float top) {
    if (!open_) {
        return Change::None;
    }
    preview_ = false;
    commit_ = false;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, top), ImGuiCond_Always);
    const float width = std::min(io.DisplaySize.x * 0.42f, 520.0f * ImGui::GetStyle().FontScaleDpi);
    ImGui::SetNextWindowSize(ImVec2(width, io.DisplaySize.y - top), ImGuiCond_Always);
    ImGui::Begin("Map tuning (Tab)", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 9.0f);

    // --- Seed
    ImGui::SeparatorText("Seed");
    std::uint64_t s = seed;
    if (ImGui::InputScalar("seed (hex)", ImGuiDataType_U64, &s, nullptr, nullptr, "%016llX",
                           ImGuiInputTextFlags_CharsHexadecimal |
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
        seed = s;
        commit_ = true;
    }
    if (ImGui::Button("< previous")) {
        --seed;
        commit_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("next >")) {
        ++seed;
        commit_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("random")) {
        seed = (static_cast<std::uint64_t>(SDL_rand_bits()) << 32u) ^ SDL_rand_bits() ^
               SDL_GetPerformanceCounter();
        commit_ = true;
    }

    // --- Stats
    ImGui::SeparatorText("This map");
    if (stats.fallback) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "FALLBACK: no attempt met the rules (%d tried)", stats.attempts);
    } else {
        ImGui::Text("attempt %d   gen %.1f ms   bake %.0f ms", stats.attempts, stats.gen_ms,
                    stats.bake_ms);
    }
    ImGui::Text("formations %.1f%%   buildable %.0f%%", stats.formation_pct, stats.buildable_pct);
    ImGui::Text("spawn walks: %d, %d, %d steps", stats.spawn_walk[0], stats.spawn_walk[1],
                stats.spawn_walk[2]);

    // --- Generator numbers
    if (ImGui::CollapsingHeader("Rock formations", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider("count min", &params.formations_min, 0, 1024);
        slider("count max", &params.formations_max, 0, 1024);
        slider("radius min", &params.formation_radius_min, 1, 64);
        slider("radius max", &params.formation_radius_max, 1, 64);
        slider("scale min %", &params.formation_scale_min_pct, 0, 400);
        slider("scale max %", &params.formation_scale_max_pct, 0, 400);
        slider("share max %", &params.formation_share_max_pct, 0, 100);
        if (ImGui::TreeNode("roughness noise")) {
            noise_controls("rough", params.formation_rough);
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Materials")) {
        noise_controls("material", params.material_noise);
    }
    if (ImGui::CollapsingHeader("Core and spawns")) {
        slider("core area x", &params.core_area.x, 0, kMapW - 5);
        slider("core area y", &params.core_area.y, 0, kMapH - 5);
        slider("core area w", &params.core_area.w, 5, kMapW);
        slider("core area h", &params.core_area.h, 5, kMapH);
        slider("core tries", &params.core_tries, 1, 64);
        slider("spawn on edge %", &params.spawn_edge_pct, 0, 100);
        slider("spawn min walk", &params.min_spawn_dist, 0, 2000);
        slider("spawn spacing", &params.spawn_spacing, 0, kMapW);
    }
    if (ImGui::CollapsingHeader("Retries")) {
        slider("attempts", &params.attempts, 1, 256);
    }
    // Not generation: this changes what towers (and probes) reach, so the map stays as it is; fog
    // and probes update on the next regenerate.
    if (ImGui::CollapsingHeader("Towers and range")) {
        slider("light range (px)", &params.light_range_px, 8, 400);
    }

    // --- Presets (settings only)
    ImGui::SeparatorText("Presets (settings only)");
    if (!presets_listed_) {
        refresh_presets();
    }
    ImGui::InputText("name", preset_name_.data(), preset_name_.size());
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        const std::string name = sanitise(preset_name_.data());
        if (name.empty()) {
            message_ = "type a name (letters, digits, - and _)";
        } else {
            SDL_CreateDirectory(presets_dir_.c_str());
            const std::string path = presets_dir_ + name + ".txt";
            const std::string text = to_preset(params);
            message_ = SDL_SaveFile(path.c_str(), text.data(), text.size())
                           ? "saved " + name
                           : std::string("save failed: ") + SDL_GetError();
            refresh_presets();
        }
    }
    for (const std::string& name : presets_) {
        if (ImGui::Selectable(name.c_str())) {
            std::string errors;
            GenParams loaded;
            const bool ok = load_preset_file(presets_dir_ + name + ".txt", loaded, errors);
            params = loaded;
            commit_ = true;
            message_ = ok ? "loaded " + name : "loaded " + name + " with problems:\n" + errors;
            std::snprintf(preset_name_.data(), preset_name_.size(), "%s", name.c_str());
        }
    }
    if (ImGui::Button("Reset to defaults")) {
        params = GenParams{};
        commit_ = true;
        message_ = "defaults";
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh list")) {
        refresh_presets();
    }
    if (!message_.empty()) {
        ImGui::TextWrapped("%s", message_.c_str());
    }
    ImGui::TextDisabled("folder: %s", presets_dir_.c_str());

    ImGui::PopItemWidth();
    ImGui::End();

    if (preview_ || commit_) {
        validate(params);
    }
    if (commit_) {
        return Change::Commit;
    }
    return preview_ ? Change::Preview : Change::None;
}

} // namespace pixeltd::game
