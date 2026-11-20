#pragma once

#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// The terrain rules (design/world.md, Terrain and Building; #32), on the hidden grid. The ground
// is flat:
// - rock (mountains and outcrops) blocks walking, building, sight and direct fire;
// - deep water blocks walking and building (not sight);
// - shallow water, mud and marsh can be walked, not built on;
// - a tree blocks building, not walking or sight.

[[nodiscard]] inline bool is_rock(const Map& map, Cell c) noexcept {
    return map.rock.in_bounds(c) && map.rock.at(c) != 0;
}

// True if the cell is on the map and can be walked on: not rock, not deep water.
[[nodiscard]] inline bool is_walkable(const Map& map, Cell c) noexcept {
    return map.rock.in_bounds(c) && map.rock.at(c) == 0 && map.ground.at(c) != Ground::Deep;
}

// True if the cell is on the map and can be built on: soil, rich soil, sand or gravel, with no
// tree and no rock.
[[nodiscard]] inline bool is_buildable(const Map& map, Cell c) noexcept {
    if (!map.rock.in_bounds(c) || map.rock.at(c) != 0 || map.plant.at(c) == Plant::Tree) {
        return false;
    }
    const Ground g = map.ground.at(c);
    return g == Ground::Soil || g == Ground::RichSoil || g == Ground::Sand || g == Ground::Gravel;
}

// True if something can walk from cell a to its 4-neighbour b: both walkable. Symmetric.
[[nodiscard]] bool can_step(const Map& map, Cell a, Cell b) noexcept;

// True if every cell in `cells` is buildable.
[[nodiscard]] bool footprint_ok(const Map& map, const Rect& cells) noexcept;

// The same for a size x size block of cells with its top-left at `top_left`.
[[nodiscard]] inline bool footprint_ok(const Map& map, Cell top_left, int size) noexcept {
    return footprint_ok(map, Rect{top_left.x, top_left.y, size, size});
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
[[nodiscard]] bool placement_ok(const Map& map, SubCell top_left, int size_sub) noexcept;

// True if a spawn's 2x2 cell block touches a map edge.
[[nodiscard]] constexpr bool on_map_edge(Cell top_left) noexcept {
    return top_left.x == 0 || top_left.y == 0 || top_left.x == kMapW - 2 || top_left.y == kMapH - 2;
}

} // namespace pixeltd
