#include "core/los.hpp"

#include <cstdlib>

namespace pixeltd {

Height height_at(const Map& map, Cell c) noexcept {
    return map.terrain.in_bounds(c) && map.terrain.at(c) == Terrain::High ? Height::High
                                                                          : Height::Low;
}

bool clear_shot(const Map& map, PointPx from, PointPx to, Height shooter, Shot shot) noexcept {
    const bool rocks_block = shooter == Height::Low && shot == Shot::Direct;
    const auto blocks = [&](int cx, int cy) {
        if (!map.terrain.in_bounds(cx, cy)) {
            return false;
        }
        const Terrain t = map.terrain.at(cx, cy);
        return t == Terrain::Cliff || (rocks_block && t == Terrain::Rock);
    };

    // A grid walk along the segment (Amanatides & Woo), comparing the distances to the next
    // vertical and horizontal cell borders by cross-multiplication instead of division.
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    const int step_x = dx > 0 ? 1 : dx < 0 ? -1 : 0;
    const int step_y = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    const long long adx = std::abs(dx);
    const long long ady = std::abs(dy);
    int cx = from.x / kCellPx;
    int cy = from.y / kCellPx;
    const int ex = to.x / kCellPx;
    const int ey = to.y / kCellPx;
    // A start exactly on a border, heading back across it, is already in the cell beyond.
    if (step_x < 0 && from.x % kCellPx == 0) {
        --cx;
    }
    if (step_y < 0 && from.y % kCellPx == 0) {
        --cy;
    }
    if (blocks(cx, cy)) {
        return false;
    }
    const int limit = std::abs(ex - cx) + std::abs(ey - cy) + 2;
    for (int i = 0; i < limit && (cx != ex || cy != ey); ++i) {
        // Distance (along each axis) from the start to the next border, scaled by the other
        // axis's length, so the two are comparable.
        const long long bx = step_x > 0 ? (cx + 1) * kCellPx : cx * kCellPx;
        const long long by = step_y > 0 ? (cy + 1) * kCellPx : cy * kCellPx;
        const long long tx = step_x == 0 ? -1 : std::abs(bx - from.x) * ady;
        const long long ty = step_y == 0 ? -1 : std::abs(by - from.y) * adx;
        if (ty < 0 || (tx >= 0 && tx < ty)) {
            cx += step_x;
        } else if (tx < 0 || ty < tx) {
            cy += step_y;
        } else {
            // Exactly through a corner: both cells beside it count.
            if (blocks(cx + step_x, cy) || blocks(cx, cy + step_y)) {
                return false;
            }
            cx += step_x;
            cy += step_y;
        }
        if (blocks(cx, cy)) {
            return false;
        }
    }
    return true;
}

} // namespace pixeltd
