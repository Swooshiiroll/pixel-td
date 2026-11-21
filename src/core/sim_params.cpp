#include "core/sim_params.hpp"

#include <algorithm>

#include "core/terrain.hpp"

namespace pixeltd {
namespace {

void fit(int& v, int lo, int hi, int& changed) {
    const int c = std::clamp(v, lo, hi);
    if (c != v) {
        v = c;
        ++changed;
    }
}

void fit(TowerParams& t, int& changed) {
    fit(t.range_px, 8, 960, changed);
    fit(t.damage, 1, 100000, changed);
    fit(t.fire_interval_ms, 16, 60000, changed);
    fit(t.shot_speed_px_s, 30, 3000, changed);
    fit(t.health, 1, 1000000, changed);
}

} // namespace

int validate(SimParams& p) noexcept {
    int changed = 0;
    fit(p.walker_speed_px_s, 4, 256, changed);
    fit(p.walker_health, 1, 100000, changed);
    fit(p.walker_attack_per_s, 1, 10000, changed);
    fit(p.walker_core_hit, 0, 100000, changed);
    fit(p.light_block_health, 1, 1000000, changed);
    fit(p.heavy_block_health, 1, 1000000, changed);
    fit(p.core_health, 1, 1000000, changed);
    fit(p.tree_speed_pct, 10, 100, changed);
    fit(p.mud_speed_pct, 10, 100, changed);
    fit(p.marsh_speed_pct, 10, 100, changed);
    fit(p.shallow_speed_pct, 10, 100, changed);
    fit(p.stream_interval_ms, 100, 60000, changed);
    fit(p.light_tower, changed);
    fit(p.heavy_tower, changed);
    fit(p.route_delay_ticks, 0, 120, changed);
    return changed;
}

int speed_pct(const Map& map, Cell c, const SimParams& p) noexcept {
    if (!is_walkable(map, c)) {
        return 0;
    }
    int pct = 100;
    switch (map.ground.at(c)) {
    case Ground::Shallow:
        pct = p.shallow_speed_pct;
        break;
    case Ground::Mud:
        pct = p.mud_speed_pct;
        break;
    case Ground::Marsh:
        pct = p.marsh_speed_pct;
        break;
    default:
        break;
    }
    if (map.plant.at(c) == Plant::Tree) {
        pct = std::min(pct, p.tree_speed_pct);
    }
    return pct;
}

} // namespace pixeltd
