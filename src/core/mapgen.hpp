#pragma once

#include <cstdint>

#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// The map generator (design/world.md, How a map is made). From the seed alone it makes the
// heightmap (hills, mesas, cliffs), rock formations and materials, then places the core in the
// central third and the spawns. Deterministic: the same seed and params give the same map on every
// platform (integer maths only, randomness only from pixeltd::Rng). Bad attempts are rejected and
// regenerated from a derived sub-seed; after params.attempts it returns the known-safe fallback.
[[nodiscard]] Map generate_map(std::uint64_t seed, const GenParams& params = {});

// The known-safe layout: flat ground, the core in the middle, spawns in three corners.
[[nodiscard]] Map make_fallback_map(std::uint64_t seed, const GenParams& params = {});

// Walking steps from the core to every cell (4-neighbour moves that can_step allows; the core's
// own cells excluded). -1 where unreachable, or everywhere if the map has no core.
[[nodiscard]] Grid<int> walk_distance_to_core(const Map& map, const GenParams& params = {});

} // namespace pixeltd
