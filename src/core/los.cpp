#include "core/los.hpp"

#include <algorithm>
#include <cstdlib>

namespace pixeltd {

bool clear_shot(const Map& map, PointPx from, PointPx to, Shot shot) noexcept {
    if (shot == Shot::Lobbed) {
        return true; // lobbed shots arc over rock formations
    }
    const long long dx = to.x - from.x;
    const long long dy = to.y - from.y;
    const int start_x = from.x / kCellPx;
    const int start_y = from.y / kCellPx;
    const int end_x = to.x / kCellPx;
    const int end_y = to.y / kCellPx;

    // Does cell (cx, cy) block? Rock formations do; the shooter's and the target's own cells never
    // block.
    const auto blocks = [&](int cx, int cy) {
        if (!map.rock.in_bounds(cx, cy) || (cx == start_x && cy == start_y) ||
            (cx == end_x && cy == end_y)) {
            return false;
        }
        return map.rock.at(cx, cy) != 0;
    };

    // A grid walk along the segment (Amanatides & Woo), comparing the distances to the next
    // vertical and horizontal cell borders by cross-multiplication instead of division.
    const int step_x = dx > 0 ? 1 : dx < 0 ? -1 : 0;
    const int step_y = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    const long long adx = std::abs(dx);
    const long long ady = std::abs(dy);
    int cx = start_x;
    int cy = start_y;
    // A start exactly on a border, heading back across it, is already in the cell beyond.
    if (step_x < 0 && from.x % kCellPx == 0) {
        --cx;
    }
    if (step_y < 0 && from.y % kCellPx == 0) {
        --cy;
    }
    const int limit = std::abs(end_x - cx) + std::abs(end_y - cy) + 2;
    for (int i = 0; i < limit && (cx != end_x || cy != end_y); ++i) {
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
