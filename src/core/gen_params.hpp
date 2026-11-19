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
    // --- Rock formations: noise-edged blobs, the only obstacle on the flat ground (#30). They
    // block walking, building, sight and direct fire.
    NoiseSpec formation_rough{6}; // shapes their edges and tops
    int formations_min = 108;     // twice the count before #30, to make up for the cliffs
    int formations_max = 216;
    int formation_radius_min = 5; // cells; formations are about 10 to 32 cells across
    int formation_radius_max = 16;
    int formation_scale_min_pct = 45; // the roughness scales the radius^2 between these
    int formation_scale_max_pct = 155;
    int formation_share_max_pct = 15; // an attempt with more of the map under rock is rejected

    // --- Materials (looks only): low-frequency noise picks one per cell.
    NoiseSpec material_noise{64};

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
