#pragma once

#include <cstdint>

#include "core/grid.hpp"
#include "core/los.hpp"
#include "core/map.hpp"

namespace pixeltd {

// Explored ground (design/world.md, Visibility). The terrain is always visible; unexplored low
// ground is hazed, and path under the haze looks like plain ground. Cells get explored around
// the core when it's placed, and by towers within their range and direct sight. Enemies don't
// explore. Once explored, a cell stays explored.
class Fog {
  public:
    Fog() : explored_(kMapW, kMapH, 0) {}

    void reset() { explored_.fill(0); }

    // Developer tool (map shots): everything explored.
    void explore_all() { explored_.fill(1); }

    // When the core is placed: every cell within `range_cells` of the nearest core cell (cell
    // centres, Euclidean), and the whole ramp. Returns the bounding box of the cells explored.
    Rect explore_core(const Map& map, int range_cells);

    // A viewer (tower or probe) at `centre` explores every cell whose centre is within
    // `range_px` and that it can see (direct sight from its height). Returns the bounding box
    // of newly explored cells (empty if none).
    Rect explore_from(const Map& map, PointPx centre, int range_px, Height viewer);

    [[nodiscard]] bool explored(Cell c) const noexcept {
        return explored_.in_bounds(c) && explored_.at(c) != 0;
    }

    // Enemies on unexplored cells are hidden; this is the check they'll use.
    [[nodiscard]] bool visible(Cell c) const noexcept { return explored(c); }

    [[nodiscard]] const Grid<std::uint8_t>& cells() const noexcept { return explored_; }

  private:
    Grid<std::uint8_t> explored_;
};

} // namespace pixeltd
