#pragma once

#include <cstdint>
#include <optional>
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

// The legal core spot nearest the zone's centre (ties: first in row-major order). For
// --core auto and the tests.
[[nodiscard]] std::optional<Cell> central_core_spot(const Map& map);

// Stage 2 (design/world.md): places the core at `spot`, then generates the spawn on a map edge
// and the path to the core from the map's seed and the spot. Deterministic per (seed, spot).
// Returns false, leaving the map unchanged, if the spot isn't legal.
bool generate_stage2(Map& map, Cell spot, const GenParams& params = {});

// A legal light-tower spot (top-left of 2x2): all ground or all high ground, no path, no core.
[[nodiscard]] bool is_light_spot(const Map& map, Cell top_left) noexcept;

// The build-space guarantee: every params.build_window spine cells have a legal light-tower spot
// within params.light_range_cells of one of them (centre to centre).
[[nodiscard]] bool build_space_ok(const Map& map, const GenParams& params = {});

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
    [[nodiscard]] bool routable(Cell spot) const { return !regions_of(spot).empty(); }

    // True if, from the core at `spot`, a map edge at least params.min_spawn_dist path steps away
    // can be reached: so stage 2 can put the spawn far enough away.
    [[nodiscard]] bool far_edge_reachable(Cell spot, const Rect& zone) const;

    // Path steps from the cells around `zone` to the farthest reachable edge entry; -1 if none.
    [[nodiscard]] int farthest_edge_from(const Rect& zone) const;

    [[nodiscard]] const BrushFit& brush() const noexcept { return fit_; }

  private:
    // The connected regions of 2-wide path cells that the core at `spot` touches (directly, or
    // via its ramp) and that reach a map edge.
    [[nodiscard]] std::vector<int> regions_of(Cell spot) const;

    const Grid<Terrain>* terrain_;
    GenParams params_;
    BrushFit fit_;
    std::vector<Cell> edge_entries_; // 2-wide brush cells touching the map edge
    Grid<int> edge_dist_;            // steps from the nearest edge entry; -1 = unreachable
    Grid<int> region_;               // connected region of each 2-wide path cell; -1 = none
    int regions_ = 0;

    // farthest_edge_from's per-region results, cached for the zone they were computed for.
    mutable Rect far_zone_{};
    mutable std::vector<int> far_by_region_;
};

// The spine cells from which a 2-wide path brush touches the core at `spot` without overlapping
// it: the ring a path arrives through.
void core_access_cells(Cell spot, std::vector<Cell>& out);

} // namespace pixeltd
