#pragma once

#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// The terrain rules (design/world.md, Terrain and Building), on the hidden grid.

[[nodiscard]] inline bool is_formation(const Map& map, Cell c) noexcept {
    return map.formation.in_bounds(c) && map.formation.at(c) != 0;
}

// True if something can walk from cell a to its 4-neighbour b: both on the map, neither part of a
// rock formation, and the height step between them no bigger than the cliff step. Symmetric.
[[nodiscard]] bool can_step(const Map& map, Cell a, Cell b, const GenParams& p) noexcept;

// A cell beside a cliff face: a step to a 4-neighbour (that isn't a formation) bigger than the
// cliff step. Nothing can be built on it.
[[nodiscard]] bool is_cliff_cell(const Map& map, Cell c, const GenParams& p) noexcept;

// True if a size x size footprint with its top-left at `top_left` can be built: on the map, no
// formation or cliff cell, and its heights within the flat limit.
[[nodiscard]] bool footprint_ok(const Map& map, Cell top_left, int size,
                                const GenParams& p) noexcept;

// True if a spawn's 2x2 cell block touches a map edge.
[[nodiscard]] constexpr bool on_map_edge(Cell top_left) noexcept {
    return top_left.x == 0 || top_left.y == 0 || top_left.x == kMapW - 2 || top_left.y == kMapH - 2;
}

} // namespace pixeltd
