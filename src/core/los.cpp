#include "core/los.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>

namespace pixeltd {

int ground_at(const Map& map, PointPx p) noexcept {
    const int cx = std::clamp(p.x / kCellPx, 0, kMapW - 1);
    const int cy = std::clamp(p.y / kCellPx, 0, kMapH - 1);
    return map.height.at(cx, cy);
}

int isqrt(long long v) noexcept {
    if (v <= 0) {
        return 0;
    }
    // Digit by digit (base 4), with no division: fast enough for erosion's millions of calls.
    auto n = static_cast<unsigned long long>(v);
    unsigned long long root = 0;
    unsigned long long bit = 1ull << ((std::bit_width(n) - 1) & ~1);
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<int>(root);
}

int effective_range(int base_px, int tower_ground, int target_ground, int distance_px,
                    const GenParams& p) noexcept {
    // pct = grade * pct_per_grade, with grade = fall * 16 / distance: one division, so it
    // rounds once (towards zero).
    const long long fall = tower_ground - target_ground;
    const long long dist = std::max(distance_px, kCellPx);
    const long long raw = fall * kCellPx * p.range_pct_per_grade / dist;
    const long long cap = p.range_slope_max_pct;
    const int pct = static_cast<int>(std::clamp(raw, -cap, cap));
    return base_px * (100 + pct) / 100;
}

bool in_reach(int base_px, int tower_ground, int target_ground, long long dx, long long dy,
              const GenParams& p) noexcept {
    const long long d2 = dx * dx + dy * dy;
    const long long r = effective_range(base_px, tower_ground, target_ground, isqrt(d2), p);
    return d2 <= r * r;
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
