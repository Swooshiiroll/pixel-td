#include "core/gen_params.hpp"

#include <algorithm>

namespace pixeltd {
namespace {

// Clamps v into [lo, hi], counting a change.
void fit(int& v, int lo, int hi, int& changed) {
    const int c = std::clamp(v, lo, std::max(lo, hi));
    if (c != v) {
        v = c;
        ++changed;
    }
}

// An out-of-range enum goes back to its default (NoiseSpec's).
template <class E> void fit_enum(E& e, int count, E fallback, int& changed) {
    if (static_cast<int>(e) >= count) {
        e = fallback;
        ++changed;
    }
}

void fit_noise(NoiseSpec& n, int& changed) {
    fit(n.step, 2, kMapW / 2,
        changed); // a lattice point at least every 2 cells, at most half the map
    fit(n.octaves, 1, kMaxOctaves, changed);
    fit(n.lacunarity_pct, 10, 100, changed);
    fit(n.persistence_pct, 0, 100, changed);
    fit_enum(n.curve, kCurves, NoiseSpec{}.curve, changed);
    fit_enum(n.shape, kShapes, NoiseSpec{}.shape, changed);
    fit_enum(n.basis, kBases, NoiseSpec{}.basis, changed);
    fit(n.warp_cells, 0, 64, changed);
    fit(n.warp_step, 2, kMapW / 2, changed);
}

} // namespace

int validate(GenParams& p) noexcept {
    int changed = 0;
    for (NoiseSpec* n : {&p.fertility_noise, &p.moisture_noise, &p.mountain_noise, &p.outcrop_noise,
                         &p.cave_noise, &p.river_noise, &p.lake_rough, &p.forest_noise}) {
        fit_noise(*n, changed);
    }
    fit(p.mountain_share_pct, 0, 70, changed);
    fit(p.mountain_edge_bias, 0, 255, changed);
    fit(p.mountain_edge_reach_pct, 1, 100, changed);
    fit(p.mountain_smooth_passes, 0, 8, changed);
    fit(p.core_clear_cells, 0, 120, changed);
    fit(p.outcrop_share_pct, 0, 20, changed);
    fit(p.rock_min_cells, 1, 10000, changed);
    fit(p.cave_depth_cells, 1, 40, changed);
    fit(p.cave_share_pct, 0, 100, changed);
    fit(p.cave_min_cells, 1, 10000, changed);
    fit(p.river_pct, 0, 100, changed);
    fit(p.river_mountain_cost, 0, 1000, changed);
    fit(p.river_deep_width, 0, 40, changed);
    fit(p.river_bank_width, 1, 20, changed);
    fit(p.fords_min, 1, 12, changed);
    fit(p.fords_max, p.fords_min, 12, changed);
    fit(p.ford_length, 1, 60, changed);
    fit(p.lakes_min, 0, 64, changed);
    fit(p.lakes_max, p.lakes_min, 64, changed);
    fit(p.lake_radius_min, 2, 80, changed);
    fit(p.lake_radius_max, p.lake_radius_min, 80, changed);
    fit(p.lake_rim_cells, 1, 20, changed);
    fit(p.wet_reach_cells, 0, 40, changed);
    fit(p.wet_moisture_pct, 0, 100, changed);
    fit(p.marsh_moisture_pct, 0, 100, changed);
    fit(p.gravel_reach_cells, 0, 20, changed);
    fit(p.sand_fertility_pct, 0, 100, changed);
    fit(p.rich_fertility_pct, p.sand_fertility_pct, 100, changed);
    fit(p.forest_threshold_pct, 0, 100, changed);
    fit(p.forest_ramp_pct, 1, 100, changed);
    fit(p.tree_rich_pct, 0, 100, changed);
    fit(p.tree_soil_pct, 0, 100, changed);
    fit(p.grass_fertility_pct, 0, 100, changed);
    fit(p.bush_pct, 0, 100, changed);
    // The core area: inside the map and big enough for the core's 5 x 5 cells.
    constexpr int kCoreCells = 5;
    fit(p.core_area.x, 0, kMapW - kCoreCells, changed);
    fit(p.core_area.y, 0, kMapH - kCoreCells, changed);
    fit(p.core_area.w, kCoreCells, kMapW - p.core_area.x, changed);
    fit(p.core_area.h, kCoreCells, kMapH - p.core_area.y, changed);
    fit(p.core_tries, 1, 64, changed);
    for (int& w : p.spawn_waves) {
        fit(w, 1, 999, changed);
    }
    fit(p.spawn_edge_pct, 0, 100, changed);
    fit(p.min_spawn_dist, 0, kMapW * kMapH, changed);
    fit(p.spawn_spacing, 0, kMapW, changed);
    fit(p.light_range_px, 1, 2000, changed);
    fit(p.attempts, 1, 256, changed);
    return changed;
}

} // namespace pixeltd
