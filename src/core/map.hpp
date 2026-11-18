#pragma once

#include <cstdint>

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

// How the generator got its result; the tests assert the fallbacks are never needed.
struct GenStats {
    int stage1_attempts = 0; // terrain attempts used (1 = the first one worked)
    int zone_tries = 0;      // core-zone positions tried in the successful attempt
    bool stage1_fallback = false;
};

// One run's map. Stage 1 (the seed) fills the terrain and the core zone.
struct Map {
    std::uint64_t seed = 0;
    Grid<Terrain> terrain{kMapW, kMapH, Terrain::Ground};
    Rect zone{};
    GenStats stats{};
};

} // namespace pixeltd
