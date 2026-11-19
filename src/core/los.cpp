#include "core/los.hpp"

#include <algorithm>
#include <cstdlib>

namespace pixeltd {

int ground_at(const Map& map, PointPx p) noexcept {
    const int cx = std::clamp(p.x / kCellPx, 0, kMapW - 1);
    const int cy = std::clamp(p.y / kCellPx, 0, kMapH - 1);
    return map.height.at(cx, cy);
}

int effective_range(int base_px, int tower_ground, int target_ground, const GenParams& p) noexcept {
    // Downhill the bonus grows with the drop, with no cap; uphill the penalty has a floor.
    const int pct = std::max((tower_ground - target_ground) * p.range_pct_per_10 / 10,
                             -p.range_penalty_max_pct);
    return base_px * (100 + pct) / 100;
}

bool clear_shot(const Map& map, PointPx from, PointPx to, Shot shot, const GenParams& p) noexcept {
    const long long eye = ground_at(map, from) + p.eye_height;
    const long long aim = ground_at(map, to) + p.target_height;
    const long long dx = to.x - from.x;
    const long long dy = to.y - from.y;
    const long long len2 = dx * dx + dy * dy;
    const int start_x = from.x / kCellPx;
    const int start_y = from.y / kCellPx;
    const int end_x = to.x / kCellPx;
    const int end_y = to.y / kCellPx;

    // Does cell (cx, cy) block? Formations block every shot. For direct fire, terrain blocks if
    // it's higher than the line where the line passes the cell's centre. The shooter's and the
    // target's own cells never block.
    const auto blocks = [&](int cx, int cy) {
        if (!map.height.in_bounds(cx, cy) || (cx == start_x && cy == start_y) ||
            (cx == end_x && cy == end_y)) {
            return false;
        }
        if (map.formation.at(cx, cy) != 0) {
            return true;
        }
        if (shot == Shot::Lobbed || len2 == 0) {
            return false;
        }
        // How far along the line (0..len2) the cell's centre lies, then the line's height there.
        const long long px = cx * kCellPx + kCellPx / 2 - from.x;
        const long long py = cy * kCellPx + kCellPx / 2 - from.y;
        const long long t = std::clamp(px * dx + py * dy, 0LL, len2);
        const long long line = eye + (aim - eye) * t / len2;
        return map.height.at(cx, cy) > line;
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
