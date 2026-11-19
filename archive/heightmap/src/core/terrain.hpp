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

// True if every cell in `cells` can be built on together: on the map, no formation or cliff cell,
// and their heights within the flat limit.
[[nodiscard]] bool footprint_ok(const Map& map, const Rect& cells, const GenParams& p) noexcept;

// The same for a size x size block of cells with its top-left at `top_left`.
[[nodiscard]] inline bool footprint_ok(const Map& map, Cell top_left, int size,
                                       const GenParams& p) noexcept {
    return footprint_ok(map, Rect{top_left.x, top_left.y, size, size}, p);
}

// The cells a size_sub x size_sub subcell footprint covers (any part of). A 3-subcell side always
// covers 2 cells; 6 covers 3 or 4 and 9 covers 5, depending on alignment.
[[nodiscard]] constexpr Rect cells_under(SubCell top_left, int size_sub) noexcept {
    const int x0 = top_left.x / kSubPerCell;
    const int y0 = top_left.y / kSubPerCell;
    const int x1 = (top_left.x + size_sub - 1) / kSubPerCell;
    const int y1 = (top_left.y + size_sub - 1) / kSubPerCell;
    return {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

// True if a tower or core footprint (size_sub x size_sub subcells, top-left at `top_left`) can go
// there: inside the subgrid, and every cell it covers buildable together (footprint_ok).
[[nodiscard]] bool placement_ok(const Map& map, SubCell top_left, int size_sub,
                                const GenParams& p) noexcept;

// True if a spawn's 2x2 cell block touches a map edge.
[[nodiscard]] constexpr bool on_map_edge(Cell top_left) noexcept {
    return top_left.x == 0 || top_left.y == 0 || top_left.x == kMapW - 2 || top_left.y == kMapH - 2;
}

} // namespace pixeltd
