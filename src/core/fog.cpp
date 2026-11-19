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

Rect Fog::explore_core(const Map& map, int range_px) {
    Rect box{};
    if (!map.core) {
        return box;
    }
    const RectPx k = rect_of(*map.core, kCoreSub);
    const long long r2 = static_cast<long long>(range_px) * range_px;
    const int x0 = std::max(0, (k.x - range_px) / kCellPx);
    const int y0 = std::max(0, (k.y - range_px) / kCellPx);
    const int x1 = std::min(kMapW - 1, (k.x + k.w + range_px) / kCellPx);
    const int y1 = std::min(kMapH - 1, (k.y + k.h + range_px) / kCellPx);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            // Distance from this cell's centre to the nearest point of the core's footprint.
            const PointPx c = centre_of(Cell{x, y});
            const long long dx = std::max({k.x - c.x, 0, c.x - (k.x + k.w)});
            const long long dy = std::max({k.y - c.y, 0, c.y - (k.y + k.h)});
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
    // The farthest any cell could be in reach: against the lowest possible ground.
    const int reach = max_reach(base_px, tower, p);
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
