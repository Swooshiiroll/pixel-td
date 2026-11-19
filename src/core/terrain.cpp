#include "core/terrain.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace pixeltd {
namespace {

constexpr std::array<Cell, 4> kNeighbours = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

} // namespace

bool can_step(const Map& map, Cell a, Cell b, const GenParams& p) noexcept {
    if (!map.height.in_bounds(a) || !map.height.in_bounds(b) || is_formation(map, a) ||
        is_formation(map, b)) {
        return false;
    }
    return std::abs(map.height.at(a) - map.height.at(b)) <= p.cliff_step;
}

bool is_cliff_cell(const Map& map, Cell c, const GenParams& p) noexcept {
    if (!map.height.in_bounds(c)) {
        return false;
    }
    const int h = map.height.at(c);
    for (const Cell d : kNeighbours) {
        const Cell n{c.x + d.x, c.y + d.y};
        if (map.height.in_bounds(n) && !is_formation(map, n) &&
            std::abs(map.height.at(n) - h) > p.cliff_step) {
            return true;
        }
    }
    return false;
}

bool placement_ok(const Map& map, SubCell top_left, int size_sub, const GenParams& p) noexcept {
    if (top_left.x < 0 || top_left.y < 0 || top_left.x + size_sub > kSubW ||
        top_left.y + size_sub > kSubH) {
        return false;
    }
    return footprint_ok(map, cells_under(top_left, size_sub), p);
}

bool footprint_ok(const Map& map, const Rect& cells, const GenParams& p) noexcept {
    if (cells.w <= 0 || cells.h <= 0 || !map.height.in_bounds(cells.x, cells.y) ||
        !map.height.in_bounds(cells.x + cells.w - 1, cells.y + cells.h - 1)) {
        return false;
    }
    int lo = 255;
    int hi = 0;
    for (int y = cells.y; y < cells.y + cells.h; ++y) {
        for (int x = cells.x; x < cells.x + cells.w; ++x) {
            const Cell c{x, y};
            if (is_formation(map, c) || is_cliff_cell(map, c, p)) {
                return false;
            }
            lo = std::min(lo, static_cast<int>(map.height.at(c)));
            hi = std::max(hi, static_cast<int>(map.height.at(c)));
        }
    }
    return hi - lo <= p.flat_limit;
}

} // namespace pixeltd
