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

// The centre of a cell, and of a square subcell footprint (towers, the core) whose top-left
// subcell is `top_left`. A 3x3 light tower's centre is the middle of its middle subcell.
[[nodiscard]] constexpr PointPx centre_of(Cell c) noexcept {
    return {c.x * kCellPx + kCellPx / 2, c.y * kCellPx + kCellPx / 2};
}
[[nodiscard]] constexpr PointPx centre_of(SubCell top_left, int size_sub) noexcept {
    return {top_left.x * kSubPx + size_sub * kSubPx / 2,
            top_left.y * kSubPx + size_sub * kSubPx / 2};
}

// The pixel rectangle of a subcell footprint.
struct RectPx {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};
[[nodiscard]] constexpr RectPx rect_of(SubCell top_left, int size_sub) noexcept {
    return {top_left.x * kSubPx, top_left.y * kSubPx, size_sub * kSubPx, size_sub * kSubPx};
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

// The integer square root: the largest r with r * r <= v (0 for v <= 0).
[[nodiscard]] int isqrt(long long v) noexcept;

// Range by grade (design/world.md, Line of sight and range; #27). The reach depends on how the
// ground falls or rises between a tower and its target, not on how high the tower stands:
// - the grade is the fall per cell, (tower_ground - target_ground) * 16 / distance_px height units
//   (a distance under one cell counts as one cell), positive downhill;
// - the range changes by grade * range_pct_per_grade %, at most +-range_slope_max_pct.
// Flat ground at any altitude gives the base range. Integer maths only.
[[nodiscard]] int effective_range(int base_px, int tower_ground, int target_ground, int distance_px,
                                  const GenParams& p) noexcept;

// True if a target (dx, dy) world pixels from the tower is within its reach for those grounds.
[[nodiscard]] bool in_reach(int base_px, int tower_ground, int target_ground, long long dx,
                            long long dy, const GenParams& p) noexcept;

// The farthest any target can be: the base range stretched by the full downhill bonus.
[[nodiscard]] inline int max_reach(int base_px, const GenParams& p) noexcept {
    return base_px * (100 + p.range_slope_max_pct) / 100;
}

} // namespace pixeltd
