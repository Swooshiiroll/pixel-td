#pragma once

#include <array>
#include <cstdint>

#include "core/grid.hpp"

namespace pixeltd {

// How a noise layer blends between its lattice points.
enum class Curve : std::uint8_t {
    Linear,     // straight lines: faceted
    Smoothstep, // 3t^2 - 2t^3: smooth (the default)
    Quintic,    // 6t^5 - 15t^4 + 10t^3: smoothest
};
inline constexpr int kCurves = 3;

// What each octave's 0..255 value is turned into.
enum class Shape : std::uint8_t {
    Normal,
    Ridged,   // 255 - |2v - 255|: sharp ridges where the noise crosses its middle
    Billowed, // |2v - 255|: puffy, rounded hills
};
inline constexpr int kShapes = 3;

// One noise layer: octaves of integer value noise, optionally shaped and domain-warped. All
// (start) values. The defaults of each layer reproduce the generator's noise before #21 exactly.
struct NoiseSpec {
    int step = 64;            // cells between lattice points of the first (coarsest) octave
    int octaves = 1;          // 1..kMaxOctaves
    int lacunarity_pct = 50;  // each next octave's step, as a % of the one before
    int persistence_pct = 50; // each next octave's weight, as a % of the one before
    Curve curve = Curve::Smoothstep;
    Shape shape = Shape::Normal;
    int warp_cells = 0; // domain warp: how far (in cells) sample points are pushed; 0 = off
    int warp_step = 48; // ...and the feature size of the warp noise

    friend constexpr bool operator==(const NoiseSpec&, const NoiseSpec&) = default;
};
inline constexpr int kMaxOctaves = 6;

// Every number the map generator and the terrain rules use. All are (start) values
// (design/world.md): tuned from map shots and the in-game tuning panel, and saved as presets.
struct GenParams {
    // --- Height: octaves of value noise (by default every 64, 32 and 16 cells, weights 4:2:1),
    // stretched to 0..ground_max.
    NoiseSpec height{64, 3};
    int ground_max = 200; // ground heights are 0..ground_max

    // --- Mesas: a noise mask picks regions whose height is terraced into steps, so their edges
    // become cliff faces.
    NoiseSpec mesa_noise{48};
    int mesa_share_pct = 30;    // share of the map that's terraced
    int mesa_band = 20;         // height between terrace levels (still a cliff: > cliff_step)
    int mesa_feather_cells = 8; // terracing fades in over this many cells from a mesa's edge
    int mesa_relief = 6;        // table tops keep this much of the slope under them (0 = flat)

    // --- Cliffs and building.
    int cliff_step = 12;         // a height step bigger than this between neighbours is a cliff
    int cliff_share_min_pct = 2; // accepted share of cliff cells, or the attempt is rejected
    int cliff_share_max_pct = 25;
    int flat_limit = 6; // most height variation across a footprint that can be built on

    // --- Rock formations: noise-edged blobs that tower over the terrain.
    NoiseSpec formation_rough{6}; // shapes their edges and tops
    int formations_min = 6;
    int formations_max = 12;
    int formation_radius_min = 5; // cells; formations are about 10 to 32 cells across
    int formation_radius_max = 16;
    int formation_scale_min_pct = 45; // the roughness scales the radius^2 between these
    int formation_scale_max_pct = 155;
    int formation_height_min = 210; // they stand at formation_height_min..255
    int formation_share_max_pct = 10;

    // --- Materials (looks only): low-frequency noise picks one per cell.
    NoiseSpec material_noise{64};

    // --- The core: the seed picks a buildable spot on the 8 px subgrid whose cells all lie in the
    // central third of the map.
    Rect core_area{kMapW / 3, kMapH / 3, kMapW - 2 * (kMapW / 3), kMapH - 2 * (kMapH / 3)};
    int core_tries = 6; // core spots tried per terrain attempt before rejecting it

    // --- Spawns.
    std::array<int, 3> spawn_waves{1, 10, 20}; // one spawn per entry, opening at that wave
    int spawn_edge_pct = 50;                   // chance a spawn is on an edge (else inside)
    int min_spawn_dist = 120;                  // walking steps from the core, at least
    int spawn_spacing = 60;                    // cells between spawns, at least

    // --- Towers (until towers have their own data), sight and range (design/world.md).
    int light_range_px = 72;        // a light tower's base range (4.5 cells)
    int eye_height = 10;            // a tower sees and shoots from this far above its ground
    int target_height = 4;          // ...at a point this far above the target's ground
    int range_pct_per_10 = 5;       // range change per 10 height units above (or below) the target;
                                    // shooting down, it keeps growing with the drop (no cap)
    int range_penalty_max_pct = 25; // shooting up: at most this much shorter

    // --- Retries.
    int attempts = 64; // terrain attempts, then the known-safe fallback layout

    friend constexpr bool operator==(const GenParams&, const GenParams&) = default;
};

// Clamps every value to a range the generator handles (so a tuning panel or a hand-edited preset
// can't crash it or make it loop forever). Returns the number of values it changed.
int validate(GenParams& p) noexcept;

} // namespace pixeltd
