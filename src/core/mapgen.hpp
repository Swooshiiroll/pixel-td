#pragma once

#include <cstdint>
#include <vector>

#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd {

// Stage 1 of the map generator (design/world.md): terrain and one core zone, from the seed alone.
// Deterministic: the same seed and params give the same map on every platform (integer maths
// only, randomness only from pixeltd::Rng). Bad maps are rejected and regenerated from a derived
// sub-seed; after params.stage1_attempts it returns the known-safe fallback layout.
[[nodiscard]] Map generate_stage1(std::uint64_t seed, const GenParams& params = {});

// The known-safe layout: flat ground with the zone near the right edge.
[[nodiscard]] Map make_fallback_map(std::uint64_t seed, const GenParams& params = {});

// A core spot is the top-left cell of the 6x6 core. Legal: fully inside the zone, and all ground
// or all high ground (never on a blocker, never straddling heights).
[[nodiscard]] bool is_legal_core_spot(const Map& map, Cell spot) noexcept;

// Every legal core spot in the zone, in row-major order.
[[nodiscard]] std::vector<Cell> legal_core_spots(const Map& map);

// Path brushes: a path of width w drawn at spine cell c covers the w x w square of cells whose
// top-left is c - (w - 1) / 2 on both axes. BrushFit answers "does a w-wide brush fit on ground
// here" in O(1).
class BrushFit {
  public:
    static constexpr int kMaxBrush = 6;

    explicit BrushFit(const Grid<Terrain>& terrain);

    // Side of the largest all-ground square whose top-left is (x, y), capped at kMaxBrush.
    [[nodiscard]] int square_at(int x, int y) const noexcept;
    [[nodiscard]] bool fits(Cell spine, int width) const noexcept;

  private:
    Grid<std::uint8_t> square_;
};

// Which core spots can route a path to a map edge, and how far an edge can be. Stage 1 accepts a
// zone with it; the tests use it to check every legal spot.
class Reachability {
  public:
    Reachability(const Grid<Terrain>& terrain, const GenParams& params);

    // True if a 2-wide path can run from the core at `spot` to a map edge. A high-ground core may
    // use a ramp: high cells of its own plateau within params.ramp_max cells of it.
    [[nodiscard]] bool routable(Cell spot) const;

    // Path steps from the cells around `zone` to the farthest reachable edge entry; -1 if none.
    [[nodiscard]] int farthest_edge_from(const Rect& zone) const;

    [[nodiscard]] const BrushFit& brush() const noexcept { return fit_; }

  private:
    const Grid<Terrain>* terrain_;
    GenParams params_;
    BrushFit fit_;
    std::vector<Cell> edge_entries_; // 2-wide brush cells touching the map edge
    Grid<int> edge_dist_;            // steps from the nearest edge entry; -1 = unreachable
};

// The spine cells from which a 2-wide path brush touches the core at `spot` without overlapping
// it: the ring a path arrives through.
void core_access_cells(Cell spot, std::vector<Cell>& out);

} // namespace pixeltd
