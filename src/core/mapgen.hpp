#pragma once

#include <array>
#include <cstdint>

#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// The raw noise behind a generated map, for the developer overlays (issue #13). Recording it never
// changes the map.
struct GenLayers {
    bool valid = false;                  // false for the fallback map (it has no noise)
    Grid<int> rough{kMapW, kMapH, 0};    // formation roughness (edges and tops), 0..255
    Grid<int> material{kMapW, kMapH, 0}; // material noise, 0..255
};

// The map generator (design/world.md, How a map is made). From the seed alone it makes the
// flat ground's materials and the rock formations (#30), then places the core in the
// central third and the spawns. Deterministic: the same seed and params give the same map on every
// platform (integer maths only, randomness only from pixeltd::Rng). Bad attempts are rejected and
// regenerated from a derived sub-seed; after params.attempts it returns the known-safe fallback.
// If `layers` isn't null, it receives the noise of the accepted attempt (or valid = false).
[[nodiscard]] Map generate_map(std::uint64_t seed, const GenParams& params = {},
                               GenLayers* layers = nullptr);

// The known-safe layout: no formations, the core in the middle, spawns in three corners.
[[nodiscard]] Map make_fallback_map(std::uint64_t seed, const GenParams& params = {});

// Walking steps from the core to every cell (4-neighbour moves that can_step allows; the core's
// own cells excluded). -1 where unreachable, or everywhere if the map has no core.
[[nodiscard]] Grid<int> walk_distance_to_core(const Map& map);

} // namespace pixeltd
