#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// The raw noise behind a generated map, for the developer overlays (issue #13). Recording it never
// changes the map.
struct GenLayers {
    bool valid = false;                   // false for the fallback map (it has no noise)
    Grid<int> mountain{kMapW, kMapH, 0};  // mountain noise, 0..255 (before the edge bias)
    Grid<int> fertility{kMapW, kMapH, 0}; // fertility, 0..255
    Grid<int> moisture{kMapW, kMapH, 0};  // moisture, 0..255
    std::vector<Cell> river;              // the river's middle line, edge to edge (empty: none)
    std::vector<Cell> fords;              // the middle of each ford, on that line
    int caves = 0;                        // cave pockets kept (each joined to the outside)
};

// The map generator (design/world.md, How a map is made). From the seed alone it makes the
// flat ground (#30): mountains with caves, a river and lakes, ground types and plants (#32), then
// places the core in the central third and the spawns. Deterministic: the same seed and params give
// the same map on every platform (integer maths only, randomness only from pixeltd::Rng). Bad
// attempts are rejected and regenerated from a derived sub-seed; after params.attempts it returns
// the known-safe fallback. If `layers` isn't null, it receives the noise of the accepted attempt
// (or valid = false).
[[nodiscard]] Map generate_map(std::uint64_t seed, const GenParams& params = {},
                               GenLayers* layers = nullptr);

// The known-safe layout: open soil, the core in the middle, spawns in three corners.
[[nodiscard]] Map make_fallback_map(std::uint64_t seed, const GenParams& params = {});

// Walking steps from the core to every cell (4-neighbour moves that can_step allows; the core's
// own cells excluded). -1 where unreachable, or everywhere if the map has no core.
[[nodiscard]] Grid<int> walk_distance_to_core(const Map& map);

} // namespace pixeltd
