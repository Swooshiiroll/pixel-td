#pragma once

#include <cstdint>

#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/rng.hpp"

namespace pixeltd {

// Fixed-point heights for erosion: 1 height unit = 65536.
inline constexpr long long kErosionOne = 65536;

// Particle hydraulic erosion (#29). Droplets of rain fall at seeded spots and run downhill one cell
// per step. Each picks up sediment where it can carry more (steep and fast) and drops it where it
// can carry less (flattening, slowing or climbing). Over many droplets this carves branching
// valleys with steep banks and fills the low ground with sediment.
//
// It's deterministic: integer maths only (heights in 1/65536 units), and the droplets' starts come
// from `rng` in order. It returns the sediment that left the map: what droplets still carried when
// they ran off an edge, dried up or stopped in a pit. Before minus after equals that exactly.
long long erode(Grid<int>& height_q16, Rng& rng, const GenParams& p);

} // namespace pixeltd
