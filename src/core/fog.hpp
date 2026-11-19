#pragma once

#include <cstdint>

#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/los.hpp"
#include "core/map.hpp"

namespace pixeltd {

// Explored ground (design/world.md, Visibility). The terrain is always visible; unexplored ground
// is hazed, and spawns and enemies on it are hidden. Cells get explored around the core at run
// start, and by towers within their reach and direct sight. Enemies don't explore. Once explored,
// a cell stays explored.
class Fog {
  public:
    Fog() : explored_(kMapW, kMapH, 0) {}

    void reset() { explored_.fill(0); }

    // Developer tool (map shots): everything explored.
    void explore_all() { explored_.fill(1); }

    // At run start: every cell whose centre is within `range_px` of the core's footprint
    // (Euclidean, to its nearest point). Returns the bounding box of the cells explored.
    Rect explore_core(const Map& map, int range_px);

    // A viewer (tower or probe) standing at `centre` with base range `base_px` explores every cell
    // within its reach for that cell's ground (in_reach: range by grade) that it can see. Returns
    // the bounding box of newly explored cells (empty if none).
    Rect explore_from(const Map& map, PointPx centre, int base_px, const GenParams& p);

    [[nodiscard]] bool explored(Cell c) const noexcept {
        return explored_.in_bounds(c) && explored_.at(c) != 0;
    }

    // Enemies on unexplored cells are hidden; this is the check they'll use.
    [[nodiscard]] bool visible(Cell c) const noexcept { return explored(c); }

    // A spawn shows once any of its 2x2 cells is explored.
    [[nodiscard]] bool spawn_found(const Spawn& s) const noexcept {
        return explored(s.cell) || explored({s.cell.x + 1, s.cell.y}) ||
               explored({s.cell.x, s.cell.y + 1}) || explored({s.cell.x + 1, s.cell.y + 1});
    }

    [[nodiscard]] const Grid<std::uint8_t>& cells() const noexcept { return explored_; }

  private:
    Grid<std::uint8_t> explored_;
};

} // namespace pixeltd
