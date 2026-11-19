#pragma once

#include <cstdint>

#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// Line of sight and range (design/world.md, Line of sight and range). The ground is flat (#30):
// - sight and direct fire are blocked by rock formations;
// - lobbed shots arc over them: nothing blocks a lobbed shot;
// - sight is always direct, so a lobbed tower needs a spotter;
// - a tower's reach is a plain circle of its range, the same everywhere.

enum class Shot : std::uint8_t { Direct, Lobbed };

// A position in world pixels (the map is 15360 x 8640).
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

// True if nothing blocks a shot of this kind from `from` to `to`. For direct fire (and sight) the
// line is walked cell by cell, and any rock formation cell on it blocks, apart from the shooter's
// and the target's own cells; where it passes exactly through a cell corner, both cells beside it
// count. Lobbed shots are never blocked. Integer maths only.
[[nodiscard]] bool clear_shot(const Map& map, PointPx from, PointPx to, Shot shot) noexcept;

// Sight: always direct.
[[nodiscard]] inline bool can_see(const Map& map, PointPx from, PointPx to) noexcept {
    return clear_shot(map, from, to, Shot::Direct);
}

// True if a target (dx, dy) world pixels from the tower is within its reach: a circle of radius
// range_px.
[[nodiscard]] constexpr bool in_reach(int range_px, long long dx, long long dy) noexcept {
    return dx * dx + dy * dy <= static_cast<long long>(range_px) * range_px;
}

} // namespace pixeltd
