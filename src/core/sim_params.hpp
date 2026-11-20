#pragma once

#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// Every number the walkers, blocks and core use in phase 1 of pathfinding and mazing (#11;
// design/pathfinding.md). All are (start) values, tuned live in the tuning panel.
struct SimParams {
    int walker_speed_px_s = 32;   // on open ground: 2 cells a second
    int walker_health = 100;      //
    int walker_attack_per_s = 10; // damage a second to the block it's breaking
    int walker_core_hit = 10;     // damage to the core when it gets there
    int light_block_health = 200; // a light block (24 px): 20 s for one walker to break
    int heavy_block_health = 600; // a heavy block (48 px)
    int core_health = 1000;       //
    // Speed on slow ground, as a % of full speed (the slowest that applies to a cell counts).
    int tree_speed_pct = 85;       // -15%
    int mud_speed_pct = 80;        // -20%
    int marsh_speed_pct = 75;      // -25%
    int shallow_speed_pct = 65;    // -35%
    int stream_interval_ms = 3000; // the stream sends one walker per spawn this often

    friend constexpr bool operator==(const SimParams&, const SimParams&) = default;
};

// Clamps every value into its working range. Returns how many were changed (0 for the defaults).
int validate(SimParams& p) noexcept;

// How fast a walker crosses a cell, as a % of full speed: 0 if it can't be walked, 100 on plain
// ground, otherwise the slowest of its ground (shallow water, mud, marsh) and a tree.
[[nodiscard]] int speed_pct(const Map& map, Cell c, const SimParams& p) noexcept;

// True if the slowing numbers are the same (if not, the routes need rebuilding).
[[nodiscard]] constexpr bool same_ground_speeds(const SimParams& a, const SimParams& b) noexcept {
    return a.tree_speed_pct == b.tree_speed_pct && a.mud_speed_pct == b.mud_speed_pct &&
           a.marsh_speed_pct == b.marsh_speed_pct && a.shallow_speed_pct == b.shallow_speed_pct;
}

} // namespace pixeltd
