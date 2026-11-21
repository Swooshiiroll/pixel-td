#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/gen_params.hpp"
#include "core/map.hpp"
#include "core/sim.hpp"
#include "core/sim_params.hpp"

namespace pixeltd::game {

// Reads a preset file over a copy of the defaults (so the result is exactly the preset). False if
// the file can't be read or has errors (listed in `errors`; the settings are still usable).
bool load_preset_file(const std::string& path, GenParams& out, std::string& errors);

// What the generator produced, for the panel's stats.
struct MapStats {
    int attempts = 0;
    bool fallback = false;
    double gen_ms = 0.0;
    double bake_ms = 0.0;
    double rock_pct = 0.0;
    double water_pct = 0.0;
    double tree_pct = 0.0;
    double buildable_pct = 0.0;                // light-tower spots, sampled every 2 cells
    std::array<int, 3> spawn_walk{-1, -1, -1}; // walking steps from each spawn to the core
};
[[nodiscard]] MapStats measure(const Map& map, double gen_ms, double bake_ms);

// The test arena's part of the panel (#11 phase 2; design/test-arena.md): what it shows, the wave
// it edits, and what it asks the game to do (set by draw(), acted on and cleared by the game).
struct ArenaUi {
    const Sim* sim = nullptr;
    int selected = -1;          // the selected test tower's slot, or -1
    std::string tuning_path;    // the live-tuning file
    std::string tuning_message; // the last load's or save's result
    std::string layout_message; // the last layout load's or save's result
    WaveSpec wave{};
    bool send_wave = false;
    bool save_tuning = false;
    int targeting = -1;      // a new Targeting for the selected tower, or -1
    std::string save_layout; // a layout name to save, or empty
    std::string load_layout; // a layout name to load, or empty
};

// The developer map-tuning panel (issue #23): a Dear ImGui window with the seed, every generator
// number, live stats and presets. It edits the settings and the seed; the caller regenerates.
class TuningPanel {
  public:
    // How the map should be updated after a frame of the panel.
    enum class Change : std::uint8_t {
        None,
        Preview, // a slider is being dragged: regenerate (the caller throttles it)
        Commit,  // a value was set (slider released, combo, button, seed): regenerate fully
    };

    // Presets are saved to and loaded from `presets_dir`, layouts from `layouts_dir` (each created
    // on the first save).
    TuningPanel(std::string presets_dir, std::string layouts_dir);

    void toggle() noexcept { open_ = !open_; }
    void set_open(bool open) noexcept { open_ = open; }
    [[nodiscard]] bool open() const noexcept { return open_; }

    // Draws the panel (between ImGui::NewFrame and ImGui::Render), editing `params` and `seed`,
    // the walkers', blocks' and test towers' numbers in `sim` (#11; sim_changed() says if they
    // changed, which needs no new map), and the arena's wave and requests in `arena`. `top` is
    // where the window starts (below the HUD bar), in window pixels.
    Change draw(GenParams& params, std::uint64_t& seed, const MapStats& stats, float top,
                SimParams& sim, ArenaUi& arena);
    [[nodiscard]] bool sim_changed() const noexcept { return sim_changed_; }

  private:
    bool slider(const char* label, int* v, int lo, int hi);
    void noise_controls(const char* id, NoiseSpec& n);
    void refresh_presets();
    void refresh_layouts();
    void arena_controls(SimParams& sim, ArenaUi& arena);

    std::string presets_dir_;
    bool open_ = false;
    bool preview_ = false; // set by widgets during draw()
    bool commit_ = false;
    bool sim_changed_ = false;
    std::vector<std::string> presets_;
    bool presets_listed_ = false;
    std::array<char, 64> preset_name_{};
    std::string message_; // the last preset action's result
    std::string layouts_dir_;
    std::vector<std::string> layouts_;
    bool layouts_listed_ = false;
    std::array<char, 64> layout_name_{};
};

} // namespace pixeltd::game
