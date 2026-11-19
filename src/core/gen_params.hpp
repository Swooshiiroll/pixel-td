#pragma once

#include <array>

#include "core/grid.hpp"

namespace pixeltd {

// Every number the map generator and the terrain rules use. All are (start) values
// (design/world.md): starting points tuned from map shots now, and by the live-tuning file once
// the test arena exists.
struct GenParams {
    // --- Height: three octaves of value noise (every 64, 32 and 16 cells), stretched to 0..max.
    int height_step = 64; // cells between lattice points of the coarsest octave
    int ground_max = 200; // ground heights are 0..ground_max

    // --- Mesas: a noise mask picks regions whose height is terraced into flat steps, so their
    // edges become cliff faces.
    int mesa_noise_step = 48;
    int mesa_share_pct = 30; // share of the map that's terraced
    int mesa_band = 40;      // height between terrace levels

    // --- Cliffs and building.
    int cliff_step = 12;         // a height step bigger than this between neighbours is a cliff
    int cliff_share_min_pct = 2; // accepted share of cliff cells, or the attempt is rejected
    int cliff_share_max_pct = 25;
    int flat_limit = 6; // most height variation across a footprint that can be built on

    // --- Rock formations: noise-edged blobs that tower over the terrain.
    int formations_min = 6;
    int formations_max = 12;
    int formation_radius_min = 5; // cells; formations are about 10 to 32 cells across
    int formation_radius_max = 16;
    int formation_height_min = 210; // they stand at 210..255
    int formation_share_max_pct = 10;

    // --- Materials (looks only): low-frequency noise picks one per cell.
    int material_noise_step = 64;

    // --- The core: the seed picks a buildable 6x6 spot in the central third of the map.
    Rect core_area{kMapW / 3, kMapH / 3, kMapW - 2 * (kMapW / 3), kMapH - 2 * (kMapH / 3)};
    int core_tries = 6; // core spots tried per terrain attempt before rejecting it

    // --- Spawns.
    std::array<int, 3> spawn_waves{1, 10, 20}; // one spawn per entry, opening at that wave
    int spawn_edge_pct = 50;                   // chance a spawn is on an edge (else inside)
    int min_spawn_dist = 120;                  // walking steps from the core, at least
    int spawn_spacing = 60;                    // cells between spawns, at least

    // --- Towers (until towers have their own data), sight and range (design/world.md).
    int light_range_cells = 6;
    int eye_height = 10;            // a tower sees and shoots from this far above its ground
    int target_height = 4;          // ...at a point this far above the target's ground
    int range_pct_per_10 = 5;       // range change per 10 height units above (or below) the target
    int range_bonus_max_pct = 50;   // shooting down: at most this much longer
    int range_penalty_max_pct = 25; // shooting up: at most this much shorter

    // --- Retries.
    int attempts = 64; // terrain attempts, then the known-safe fallback layout
};

} // namespace pixeltd
