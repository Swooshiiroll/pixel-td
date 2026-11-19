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

bool footprint_ok(const Map& map, Cell top_left, int size, const GenParams& p) noexcept {
    if (!map.height.in_bounds(top_left) ||
        !map.height.in_bounds(top_left.x + size - 1, top_left.y + size - 1)) {
        return false;
    }
    int lo = 255;
    int hi = 0;
    for (int y = top_left.y; y < top_left.y + size; ++y) {
        for (int x = top_left.x; x < top_left.x + size; ++x) {
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
