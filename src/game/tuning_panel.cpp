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
    int rock = 0;
    int water = 0;
    int trees = 0;
    int spots = 0;
    int buildable = 0;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            rock += map.rock.at(x, y) != 0 ? 1 : 0;
            water += map.rock.at(x, y) == 0 && is_water(map.ground.at(x, y)) ? 1 : 0;
            trees += map.plant.at(x, y) == Plant::Tree ? 1 : 0;
            if (x % 2 == 0 && y % 2 == 0) {
                ++spots;
                buildable += placement_ok(map, SubCell{x * 2, y * 2}, kLightSub) ? 1 : 0;
            }
        }
    }
    const double cells = static_cast<double>(kMapW) * kMapH;
    s.rock_pct = 100.0 * rock / cells;
    s.water_pct = 100.0 * water / cells;
    s.tree_pct = 100.0 * trees / cells;
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
    ImGui::Text("rock %.1f%%   water %.1f%%   trees %.1f%%   buildable %.0f%%", stats.rock_pct,
                stats.water_pct, stats.tree_pct, stats.buildable_pct);
    ImGui::Text("spawn walks: %d, %d, %d steps", stats.spawn_walk[0], stats.spawn_walk[1],
                stats.spawn_walk[2]);

    // --- Generator numbers
    if (ImGui::CollapsingHeader("Mountains", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider("share %", &params.mountain_share_pct, 0, 70);
        slider("edge bias", &params.mountain_edge_bias, 0, 255);
        slider("edge reach %", &params.mountain_edge_reach_pct, 1, 100);
        slider("smoothing passes", &params.mountain_smooth_passes, 0, 8);
        slider("core clearance", &params.core_clear_cells, 0, 120);
        slider("outcrops %", &params.outcrop_share_pct, 0, 20);
        slider("min rock piece (cells)", &params.rock_min_cells, 1, 500);
        if (ImGui::TreeNode("mountain noise")) {
            noise_controls("mountain", params.mountain_noise);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("outcrop noise")) {
            noise_controls("outcrop", params.outcrop_noise);
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Caves")) {
        slider("depth (cells)", &params.cave_depth_cells, 1, 40);
        slider("share %", &params.cave_share_pct, 0, 100);
        slider("min size (cells)", &params.cave_min_cells, 1, 2000);
        if (ImGui::TreeNode("cave noise")) {
            noise_controls("cave", params.cave_noise);
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Water")) {
        slider("river chance %", &params.river_pct, 0, 100);
        slider("river deep width", &params.river_deep_width, 0, 40);
        slider("river bank width", &params.river_bank_width, 1, 20);
        slider("river mountain cost", &params.river_mountain_cost, 0, 1000);
        slider("fords min", &params.fords_min, 1, 12);
        slider("fords max", &params.fords_max, 1, 12);
        slider("ford length", &params.ford_length, 1, 60);
        slider("lakes min", &params.lakes_min, 0, 64);
        slider("lakes max", &params.lakes_max, 0, 64);
        slider("lake radius min", &params.lake_radius_min, 2, 80);
        slider("lake radius max", &params.lake_radius_max, 2, 80);
        slider("lake rim", &params.lake_rim_cells, 1, 20);
        if (ImGui::TreeNode("river noise")) {
            noise_controls("river", params.river_noise);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("lake edge noise")) {
            noise_controls("lake", params.lake_rough);
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Ground")) {
        slider("wet reach (cells)", &params.wet_reach_cells, 0, 40);
        slider("wet moisture %", &params.wet_moisture_pct, 0, 100);
        slider("marsh moisture %", &params.marsh_moisture_pct, 0, 100);
        slider("gravel reach (cells)", &params.gravel_reach_cells, 0, 20);
        slider("sand below fertility %", &params.sand_fertility_pct, 0, 100);
        slider("rich from fertility %", &params.rich_fertility_pct, 0, 100);
        if (ImGui::TreeNode("fertility noise")) {
            noise_controls("fertility", params.fertility_noise);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("moisture noise")) {
            noise_controls("moisture", params.moisture_noise);
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Plants")) {
        slider("forest threshold %", &params.forest_threshold_pct, 0, 100);
        slider("forest ramp %", &params.forest_ramp_pct, 1, 100);
        slider("trees on rich soil %", &params.tree_rich_pct, 0, 100);
        slider("trees on soil %", &params.tree_soil_pct, 0, 100);
        slider("grass from fertility %", &params.grass_fertility_pct, 0, 100);
        slider("bushes %", &params.bush_pct, 0, 100);
        if (ImGui::TreeNode("forest noise")) {
            noise_controls("forest", params.forest_noise);
            ImGui::TreePop();
        }
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
