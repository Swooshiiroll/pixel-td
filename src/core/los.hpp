#pragma once

#include <cstdint>

#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// Line of sight and range over the height field (design/world.md, Line of sight and range):
// - direct fire and sight are blocked where the terrain rises above the line from the tower's eye
//   (its ground + eye_height) to the target (its ground + target_height), and by rock formations;
// - lobbed shots arc over terrain; only rock formations block them;
// - sight is always direct, so a lobbed tower needs a spotter;
// - range grows with height above the target and shrinks when shooting uphill.

enum class Shot : std::uint8_t { Direct, Lobbed };

// A position in world pixels (the map is 3840 x 2160).
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

// The ground height under a world-pixel position (the height of the cell it's in).
[[nodiscard]] int ground_at(const Map& map, PointPx p) noexcept;

// True if nothing blocks a shot of this kind from `from` to `to` (both on the ground; the eye and
// target heights are added). The line is walked cell by cell, centre to centre; where it passes
// exactly through a cell corner, both cells beside it count. Integer maths only.
[[nodiscard]] bool clear_shot(const Map& map, PointPx from, PointPx to, Shot shot,
                              const GenParams& p) noexcept;

// Sight: always direct.
[[nodiscard]] inline bool can_see(const Map& map, PointPx from, PointPx to,
                                  const GenParams& p) noexcept {
    return clear_shot(map, from, to, Shot::Direct, p);
}

// A tower's range (world pixels) against a target, from its base range and the two ground heights:
// +range_pct_per_10 % per 10 units it stands above the target, capped at +range_bonus_max_pct,
// and the same reduction uphill, down to -range_penalty_max_pct.
[[nodiscard]] int effective_range(int base_px, int tower_ground, int target_ground,
                                  const GenParams& p) noexcept;

} // namespace pixeltd
