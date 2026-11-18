#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/grid.hpp"

namespace pixeltd {

// What a cell is made of (design/world.md, Terrain).
enum class Terrain : std::uint8_t {
    Ground, // open low ground: buildable, path runs here
    High,   // plateau: buildable, range bonus, fires over rocks
    Rock,   // low blocker: blocks direct fire from low ground
    Cliff,  // tall blocker (cliffs, mountains): blocks every shot
};

[[nodiscard]] constexpr bool is_blocker(Terrain t) noexcept {
    return t == Terrain::Rock || t == Terrain::Cliff;
}

// Path cells, painted by stage 2 over the terrain.
enum class PathKind : std::uint8_t {
    None,
    Path, // on low ground
    Ramp, // the climb to a high-ground core (the only path on high ground)
};

// How the generator got its result; the tests assert the fallbacks are never needed.
struct GenStats {
    int stage1_attempts = 0; // terrain attempts used (1 = the first one worked)
    int zone_tries = 0;      // core-zone positions tried in the successful attempt
    bool stage1_fallback = false;
    int stage2_attempts = 0; // route attempts used
    bool stage2_fallback = false;
};

// One run's map. Stage 1 (the seed) fills the terrain and the core zone; stage 2 (the seed and
// the core spot) places the core and adds the spawn and the path.
struct Map {
    std::uint64_t seed = 0;
    Grid<Terrain> terrain{kMapW, kMapH, Terrain::Ground};
    Rect zone{};

    std::optional<Cell> core;         // top-left of the 6x6 core, once placed
    Cell spawn{};                     // the spine's first cell, on a map edge
    std::vector<Cell> spine;          // the path's centre line, spawn to core
    std::vector<std::uint8_t> widths; // path width (cells) at each spine cell
    Grid<PathKind> path{kMapW, kMapH, PathKind::None};
    int base_width = 0;    // the map's base path width
    int winding = 0;       // 0 = direct ... 100 = winding
    int target_length = 0; // spine cells the route aimed for

    GenStats stats{};
};

} // namespace pixeltd
