#pragma once

#include <array>
#include <cstdint>

#include "core/grid.hpp"

namespace pixeltd {

// How a value-noise layer blends between its lattice points (unused by simplex).
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
    Billowed, // |2v - 255|: puffy, rounded blobs
};
inline constexpr int kShapes = 3;

// The kind of noise a layer is made of.
enum class Basis : std::uint8_t {
    Value,   // random values on a square lattice, blended (squarish)
    Simplex, // gradient noise on a triangular lattice (no square look)
};
inline constexpr int kBases = 2;

// One noise layer: octaves of integer noise, optionally shaped and domain-warped. All (start)
// values.
struct NoiseSpec {
    int step = 64;            // cells across the first (coarsest) octave's features
    int octaves = 1;          // 1..kMaxOctaves
    int lacunarity_pct = 50;  // each next octave's step, as a % of the one before
    int persistence_pct = 50; // each next octave's weight, as a % of the one before
    Curve curve = Curve::Smoothstep;
    Shape shape = Shape::Normal;
    int warp_cells = 0; // domain warp: how far (in cells) sample points are pushed; 0 = off
    int warp_step = 48; // ...and the feature size of the warp noise
    Basis basis = Basis::Value;

    friend constexpr bool operator==(const NoiseSpec&, const NoiseSpec&) = default;
};
inline constexpr int kMaxOctaves = 6;

// A simplex layer (the new terrain layers, #32): `step` cells across, `octaves` octaves at the
// usual halving, with an optional warp.
[[nodiscard]] constexpr NoiseSpec simplex_spec(int step, int octaves, int warp_cells = 0,
                                               int warp_step = 96) noexcept {
    return {step,      octaves,       50, 50, Curve::Smoothstep, Shape::Normal, warp_cells,
            warp_step, Basis::Simplex};
}

// Every number the map generator and the terrain rules use (design/world.md; #32). All are (start)
// values: tuned from map shots and the in-game tuning panel, and saved as presets.
struct GenParams {
    // --- Fields: fertility (where things grow) and moisture (where ground is wet).
    NoiseSpec fertility_noise = simplex_spec(96, 2, 12);
    NoiseSpec moisture_noise = simplex_spec(128, 2);

    // --- Mountains: noise plus a bias towards the map edges, cut to a share of the map.
    NoiseSpec mountain_noise = simplex_spec(80, 3, 16, 160);
    int mountain_share_pct = 20;      // of the map under mountains (before caves and outcrops)
    int mountain_edge_bias = 55;      // added to the 0..255 noise at the very edge...
    int mountain_edge_reach_pct = 45; // ...fading to 0 this far in (% of half the map's height)
    int mountain_smooth_passes = 2;   // cellular-automata passes that round off specks
    int core_clear_cells = 16;        // no mountain, lake or river this close to the core area
    NoiseSpec outcrop_noise = simplex_spec(24, 1);
    int outcrop_share_pct = 1; // small outcrops scattered on open ground
    int rock_min_cells = 12;   // smaller pieces of rock are cleared (no specks)

    // --- Caves: open pockets deep inside mountains, each joined to the outside by a tunnel.
    NoiseSpec cave_noise = simplex_spec(24, 1);
    int cave_depth_cells = 5; // only cells at least this deep inside rock can open
    int cave_share_pct = 30;  // of those deep cells, the share that opens
    int cave_min_cells = 20;  // smaller pockets are filled back in

    // --- River: a weighted shortest path from one edge to the opposite one.
    int river_pct = 80; // chance a map has a river
    NoiseSpec river_noise = simplex_spec(40, 2);
    int river_mountain_cost = 60; // extra cost per cell through mountains (it skirts them)
    int river_deep_width = 6;     // cells of deep water across its middle
    int river_bank_width = 3;     // cells of shallow water each side
    int fords_min = 2;            // shallow crossings along it
    int fords_max = 4;
    int ford_length = 6; // cells of river (along it) each ford spans

    // --- Lakes: noise-edged blobs with deep middles and shallow rims.
    int lakes_min = 3;
    int lakes_max = 8;
    int lake_radius_min = 8; // cells
    int lake_radius_max = 22;
    int lake_rim_cells = 2;
    NoiseSpec lake_rough = simplex_spec(16, 1);

    // --- Ground: wet near water, gravel by rock, else by fertility.
    int wet_reach_cells = 4;     // ground this close to water can be marsh or mud...
    int wet_moisture_pct = 45;   // ...if its moisture is at least this
    int marsh_moisture_pct = 65; // and marsh (not mud) from this moisture up
    int gravel_reach_cells = 2;  // ground this close to rock is gravel
    int sand_fertility_pct = 20; // fertility below this is sand
    int rich_fertility_pct = 72; // fertility from this up is rich soil

    // --- Plants: trees clumped into forests by fertility; grass and bushes for looks.
    NoiseSpec forest_noise = simplex_spec(48, 2);
    int forest_threshold_pct = 55; // forest noise below this grows no trees...
    int forest_ramp_pct = 10;      // ...and trees reach full density this much above it
    int tree_rich_pct = 95;        // tree chance in the thickest forest on rich soil...
    int tree_soil_pct = 75;        // ...on soil
    int grass_fertility_pct = 35;  // grass from this fertility up (looks)
    int bush_pct = 4;              // bush chance on grassy cells without a tree (looks)

    // --- The core: the seed picks a buildable spot on the 8 px subgrid whose cells all lie in the
    // central third of the map.
    Rect core_area{kMapW / 3, kMapH / 3, kMapW - 2 * (kMapW / 3), kMapH - 2 * (kMapH / 3)};
    int core_tries = 6; // core spots tried per terrain attempt before rejecting it

    // --- Spawns.
    std::array<int, 3> spawn_waves{1, 10, 20}; // one spawn per entry, opening at that wave
    int spawn_edge_pct = 50;                   // chance a spawn is on an edge (else inside)
    int min_spawn_dist = 360;                  // walking steps from the core, at least
    int spawn_spacing = 180;                   // cells between spawns, at least

    // --- Towers (until towers have their own data): range is a plain circle (design/world.md).
    int light_range_px = 72; // a light tower's range (4.5 cells)

    // --- Retries.
    int attempts = 64; // terrain attempts, then the known-safe fallback layout

    friend constexpr bool operator==(const GenParams&, const GenParams&) = default;
};

// Clamps every value to a range the generator handles (so a tuning panel or a hand-edited preset
// can't crash it or make it loop forever). Returns the number of values it changed.
int validate(GenParams& p) noexcept;

} // namespace pixeltd
