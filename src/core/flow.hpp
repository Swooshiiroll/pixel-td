#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/grid.hpp"

namespace pixeltd {

// Routes to the core as a flow field (#11; design/pathfinding.md). A walker is 16 px across, and
// routes run on the 8 px subgrid: node (x, y) is the 16 px square whose top-left is subcell (x, y),
// so any 16 px gap is walkable wherever it sits, and an 8 px gap isn't.
inline constexpr int kNodeW = kSubW - 1; // 1919
inline constexpr int kNodeH = kSubH - 1; // 1079

// Step costs: walking one subcell (8 px) straight at full speed costs 100, diagonally 141. A step
// onto slower ground costs more (x 100 / its speed %), so costs are travel times.
inline constexpr std::int32_t kStraightCost = 100;
inline constexpr std::int32_t kDiagonalCost = 141;
inline constexpr std::int32_t kUnreached = 0x7fffffff;
// One block's break cost is capped, so a route's total always fits.
inline constexpr std::int32_t kMaxBreakCost = 10000000;

// What the routes are built from.
struct FlowInput {
    // Per node: how fast it's crossed, as a % of full speed (the slowest cell under it); 0 if any
    // cell under it can't be walked, or it overlaps the core.
    const Grid<std::uint8_t>* speed = nullptr;
    // Per subcell (kSubW x kSubH): the block covering it (its slot + 1), or 0.
    const Grid<std::uint16_t>* blocks = nullptr;
    // Per block slot: what breaking it costs, in step-cost units (the breach field only).
    const std::vector<std::int32_t>* break_cost = nullptr;
    // The open field treats blocks as walls; the breach field lets routes go through them at their
    // break cost.
    bool breach = false;
};

// The blocks a node overlaps (up to 4, each once, in a fixed order), as slot + 1.
struct NodeBlocks {
    std::uint16_t id[4] = {0, 0, 0, 0};
    int count = 0;

    [[nodiscard]] bool has(std::uint16_t b) const noexcept {
        for (int i = 0; i < count; ++i) {
            if (id[i] == b) {
                return true;
            }
        }
        return false;
    }
};
[[nodiscard]] NodeBlocks node_blocks(const Grid<std::uint16_t>& blocks, SubCell n) noexcept;

// True if a walker can stand on node n in this field.
[[nodiscard]] bool node_passable(const FlowInput& in, SubCell n) noexcept;

// What stepping from node a to its neighbour b costs: travel time, plus (breach field) the break
// cost of each block b overlaps that a doesn't. Assumes both are passable and neighbours.
[[nodiscard]] std::int32_t step_cost(const FlowInput& in, SubCell a, SubCell b) noexcept;

// The nodes whose square touches the edge of a footprint (size_sub subcells, top-left at
// `top_left`) from outside: where walkers reach the core.
[[nodiscard]] std::vector<SubCell> nodes_around(SubCell top_left, int size_sub);

// The travel cost from every node to the nearest target, by Dijkstra from the targets (a bucket
// queue for the open field, a heap for the breach field; fixed neighbour order and full tie-break
// keys, so it's deterministic). Nodes it can't reach hold kUnreached.
class FlowField {
  public:
    void build(const FlowInput& in, const std::vector<SubCell>& targets);

    [[nodiscard]] bool built() const noexcept { return !dist_.empty(); }
    [[nodiscard]] std::int32_t dist(SubCell n) const noexcept {
        if (dist_.empty() || n.x < 0 || n.y < 0 || n.x >= kNodeW || n.y >= kNodeH) {
            return kUnreached;
        }
        return dist_[padded(n)];
    }
    [[nodiscard]] bool reached(SubCell n) const noexcept { return dist(n) != kUnreached; }

    // The next node on the way from n: the neighbour with the lowest cost + step cost (ties in a
    // fixed order). Nothing if n is a target or can't reach one.
    [[nodiscard]] std::optional<SubCell> next(const FlowInput& in, SubCell n) const noexcept;

  private:
    // The search works on flat arrays with a 1-node border of impassable nodes, so neighbours
    // never need a bounds check. All are reused between builds.
    static constexpr int kPadW = kNodeW + 2;
    [[nodiscard]] static constexpr std::size_t padded(SubCell n) noexcept {
        return static_cast<std::size_t>(n.y + 1) * kPadW + static_cast<std::size_t>(n.x + 1);
    }
    std::vector<std::int32_t> dist_;
    std::vector<std::uint8_t> pass_;      // 1 if passable in this field
    std::vector<std::uint16_t> straight_; // the cost of a straight step onto the node
    std::vector<std::uint16_t> diagonal_; // ...and of a diagonal one
    std::vector<std::vector<std::uint32_t>> buckets_;
};

} // namespace pixeltd
