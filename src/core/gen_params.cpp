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
    fit(n.warp_cells, 0, 64, changed);
    fit(n.warp_step, 2, kMapW / 2, changed);
}

} // namespace

int validate(GenParams& p) noexcept {
    int changed = 0;
    fit_noise(p.height, changed);
    fit(p.ground_max, 1, 200, changed); // formations stand above 200
    fit_noise(p.mesa_noise, changed);
    fit(p.mesa_share_pct, 0, 100, changed);
    fit(p.mesa_band, 1, 200, changed);
    fit(p.mesa_feather_cells, 0, 64, changed);
    fit(p.mesa_relief, 0, p.mesa_band, changed);
    fit(p.cliff_step, 1, 255, changed);
    fit(p.cliff_share_min_pct, 0, 100, changed);
    fit(p.cliff_share_max_pct, p.cliff_share_min_pct, 100, changed);
    fit(p.flat_limit, 0, 255, changed);
    fit_noise(p.formation_rough, changed);
    fit(p.formations_min, 0, 512, changed);
    fit(p.formations_max, p.formations_min, 512, changed);
    fit(p.formation_radius_min, 1, 64, changed);
    fit(p.formation_radius_max, p.formation_radius_min, 64, changed);
    fit(p.formation_scale_min_pct, 0, 400, changed);
    fit(p.formation_scale_max_pct, p.formation_scale_min_pct, 400, changed);
    fit(p.formation_height_min, p.ground_max + 1, 255, changed);
    fit(p.formation_share_max_pct, 0, 100, changed);
    fit_noise(p.material_noise, changed);
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
    fit(p.eye_height, 0, 255, changed);
    fit(p.target_height, 0, 255, changed);
    fit(p.range_pct_per_grade, 0, 100, changed);
    fit(p.range_slope_max_pct, 0, 99, changed);
    fit(p.attempts, 1, 256, changed);
    return changed;
}

} // namespace pixeltd
