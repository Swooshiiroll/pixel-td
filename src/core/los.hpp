#pragma once

#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// Line of sight (design/world.md, Terrain):
//
//   shooter                       | rocks      | cliffs / mountains
//   direct fire from low ground   | blocked    | blocked
//   direct fire from high ground  | fires over | blocked
//   lobbed weapon (any height)    | arcs over  | blocked
//
// Sight, for exploring fog, is always direct: a lobbed tower behind rocks needs a spotter.

enum class Height : std::uint8_t { Low, High };
enum class Shot : std::uint8_t { Direct, Lobbed };

// A position in world pixels (the map is 1920 x 720).
struct PointPx {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(PointPx, PointPx) = default;
};

// The centre of a cell, and of a square footprint whose top-left cell is `top_left`.
[[nodiscard]] constexpr PointPx centre_of(Cell c) noexcept {
    return {c.x * kCellPx + kCellPx / 2, c.y * kCellPx + kCellPx / 2};
}
[[nodiscard]] constexpr PointPx centre_of(Cell top_left, int size) noexcept {
    return {top_left.x * kCellPx + size * kCellPx / 2, top_left.y * kCellPx + size * kCellPx / 2};
}

// The height a tower standing on this cell has.
[[nodiscard]] Height height_at(const Map& map, Cell c) noexcept;

// True if the straight line from `from` to `to` (centre to centre) crosses no cell that blocks
// this shot. When the line passes exactly through a cell corner, both cells beside the corner
// count as crossed. Integer maths only, so every platform agrees.
[[nodiscard]] bool clear_shot(const Map& map, PointPx from, PointPx to, Height shooter,
                              Shot shot) noexcept;

// Sight: always direct.
[[nodiscard]] inline bool can_see(const Map& map, PointPx from, PointPx to,
                                  Height viewer) noexcept {
    return clear_shot(map, from, to, viewer, Shot::Direct);
}

} // namespace pixeltd
