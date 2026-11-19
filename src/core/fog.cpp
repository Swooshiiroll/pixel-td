#include "core/fog.hpp"

#include <algorithm>

namespace pixeltd {
namespace {

// Grows `box` to include cell c (an empty box becomes just c).
void include(Rect& box, Cell c) {
    if (box.w == 0) {
        box = Rect{c.x, c.y, 1, 1};
        return;
    }
    const int x1 = std::max(box.x + box.w, c.x + 1);
    const int y1 = std::max(box.y + box.h, c.y + 1);
    box.x = std::min(box.x, c.x);
    box.y = std::min(box.y, c.y);
    box.w = x1 - box.x;
    box.h = y1 - box.y;
}

} // namespace

Rect Fog::explore_core(const Map& map, int range_cells) {
    Rect box{};
    if (!map.core) {
        return box;
    }
    const Cell k = *map.core;
    const long long r2 = static_cast<long long>(range_cells) * range_cells;
    for (int y = std::max(0, k.y - range_cells);
         y <= std::min(kMapH - 1, k.y + kCoreSize - 1 + range_cells); ++y) {
        for (int x = std::max(0, k.x - range_cells);
             x <= std::min(kMapW - 1, k.x + kCoreSize - 1 + range_cells); ++x) {
            // Distance from this cell to the nearest core cell, in cells.
            const long long dx = std::max({k.x - x, 0, x - (k.x + kCoreSize - 1)});
            const long long dy = std::max({k.y - y, 0, y - (k.y + kCoreSize - 1)});
            if (dx * dx + dy * dy <= r2 && explored_.at(x, y) == 0) {
                explored_.at(x, y) = 1;
                include(box, {x, y});
            }
        }
    }
    return box;
}

Rect Fog::explore_from(const Map& map, PointPx centre, int base_px, const GenParams& p) {
    Rect box{};
    const int tower = ground_at(map, centre);
    // The farthest any cell could be in reach: the base range plus the biggest height bonus.
    const int reach = base_px * (100 + p.range_bonus_max_pct) / 100;
    const int x0 = std::max(0, (centre.x - reach) / kCellPx);
    const int y0 = std::max(0, (centre.y - reach) / kCellPx);
    const int x1 = std::min(kMapW - 1, (centre.x + reach) / kCellPx);
    const int y1 = std::min(kMapH - 1, (centre.y + reach) / kCellPx);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (explored_.at(x, y) != 0) {
                continue;
            }
            const PointPx c = centre_of(Cell{x, y});
            const long long r = effective_range(base_px, tower, map.height.at(x, y), p);
            const long long dx = c.x - centre.x;
            const long long dy = c.y - centre.y;
            if (dx * dx + dy * dy <= r * r && can_see(map, centre, c, p)) {
                explored_.at(x, y) = 1;
                include(box, {x, y});
            }
        }
    }
    return box;
}

} // namespace pixeltd
