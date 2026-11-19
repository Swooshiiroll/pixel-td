#include "core/terrain.hpp"

namespace pixeltd {

bool can_step(const Map& map, Cell a, Cell b) noexcept {
    return map.formation.in_bounds(a) && map.formation.in_bounds(b) && !is_formation(map, a) &&
           !is_formation(map, b);
}

bool placement_ok(const Map& map, SubCell top_left, int size_sub) noexcept {
    if (top_left.x < 0 || top_left.y < 0 || top_left.x + size_sub > kSubW ||
        top_left.y + size_sub > kSubH) {
        return false;
    }
    return footprint_ok(map, cells_under(top_left, size_sub));
}

bool footprint_ok(const Map& map, const Rect& cells) noexcept {
    if (cells.w <= 0 || cells.h <= 0 || !map.formation.in_bounds(cells.x, cells.y) ||
        !map.formation.in_bounds(cells.x + cells.w - 1, cells.y + cells.h - 1)) {
        return false;
    }
    for (int y = cells.y; y < cells.y + cells.h; ++y) {
        for (int x = cells.x; x < cells.x + cells.w; ++x) {
            if (is_formation(map, {x, y})) {
                return false;
            }
        }
    }
    return true;
}

} // namespace pixeltd
