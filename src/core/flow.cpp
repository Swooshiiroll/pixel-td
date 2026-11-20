#include "core/flow.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <queue>

namespace pixeltd {
namespace {

// The 8 neighbours, in the fixed order every search and every walker uses.
constexpr std::array<SubCell, 8> kNeighbours = {
    {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}}};

constexpr bool in_nodes(SubCell n) noexcept {
    return n.x >= 0 && n.y >= 0 && n.x < kNodeW && n.y < kNodeH;
}

std::int32_t saturating_add(std::int32_t a, std::int32_t b) noexcept {
    const std::int64_t s = static_cast<std::int64_t>(a) + b;
    return s >= kUnreached ? kUnreached - 1 : static_cast<std::int32_t>(s);
}

// True if a walker can step from a to its neighbour b: b is passable and, for a diagonal step,
// so are both nodes beside the corner it cuts (no squeezing past a corner).
bool can_step(const FlowInput& in, SubCell a, SubCell b) noexcept {
    if (!node_passable(in, b)) {
        return false;
    }
    if (a.x != b.x && a.y != b.y) {
        return node_passable(in, {b.x, a.y}) && node_passable(in, {a.x, b.y});
    }
    return true;
}

} // namespace

NodeBlocks node_blocks(const Grid<std::uint16_t>& blocks, SubCell n) noexcept {
    NodeBlocks nb;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const std::uint16_t b = blocks.at(n.x + dx, n.y + dy);
            if (b != 0 && !nb.has(b)) {
                nb.id[nb.count++] = b;
            }
        }
    }
    return nb;
}

bool node_passable(const FlowInput& in, SubCell n) noexcept {
    if (!in_nodes(n) || in.speed->at(n.x, n.y) == 0) {
        return false;
    }
    if (in.breach) {
        return true;
    }
    const Grid<std::uint16_t>& b = *in.blocks;
    return b.at(n.x, n.y) == 0 && b.at(n.x + 1, n.y) == 0 && b.at(n.x, n.y + 1) == 0 &&
           b.at(n.x + 1, n.y + 1) == 0;
}

std::int32_t step_cost(const FlowInput& in, SubCell a, SubCell b) noexcept {
    const std::int32_t base = (a.x != b.x && a.y != b.y) ? kDiagonalCost : kStraightCost;
    std::int32_t cost = base * 100 / static_cast<std::int32_t>(in.speed->at(b.x, b.y));
    if (in.breach) {
        const NodeBlocks into = node_blocks(*in.blocks, b);
        if (into.count > 0) {
            const NodeBlocks from = node_blocks(*in.blocks, a);
            for (int i = 0; i < into.count; ++i) {
                if (!from.has(into.id[i])) {
                    cost = saturating_add(cost, (*in.break_cost)[into.id[i] - 1u]);
                }
            }
        }
    }
    return cost;
}

std::vector<SubCell> nodes_around(SubCell top_left, int size_sub) {
    std::vector<SubCell> out;
    const int fx = top_left.x;
    const int fy = top_left.y;
    const int s = size_sub;
    for (int y = fy - 1; y <= fy + s - 1; ++y) { // left and right sides
        out.push_back({fx - 2, y});
        out.push_back({fx + s, y});
    }
    for (int x = fx - 1; x <= fx + s - 1; ++x) { // top and bottom sides
        out.push_back({x, fy - 2});
        out.push_back({x, fy + s});
    }
    std::erase_if(out, [](SubCell n) { return !in_nodes(n); });
    return out;
}

void FlowField::build(const FlowInput& in, const std::vector<SubCell>& targets) {
    constexpr std::size_t kPadH = kNodeH + 2;
    const std::size_t total = static_cast<std::size_t>(kPadW) * kPadH;
    dist_.assign(total, kUnreached);
    pass_.assign(total, 0);
    straight_.resize(total);
    diagonal_.resize(total);

    // Each node once: passable in this field (bit 0), holds a block (bit 1), and its step costs.
    constexpr std::uint8_t kPass = 1;
    constexpr std::uint8_t kHasBlock = 2;
    const Grid<std::uint16_t>& blocks = *in.blocks;
    std::uint8_t slowest = 100; // sizes the bucket ring: the largest step there is
    for (int y = 0; y < kNodeH; ++y) {
        const std::uint16_t* r0 = &blocks.at(0, y);
        const std::uint16_t* r1 = &blocks.at(0, y + 1);
        for (int x = 0; x < kNodeW; ++x) {
            const std::uint8_t s = in.speed->at(x, y);
            if (s == 0) {
                continue;
            }
            slowest = std::min(slowest, s);
            const bool blocked = (r0[x] | r0[x + 1] | r1[x] | r1[x + 1]) != 0;
            if (blocked && !in.breach) {
                continue;
            }
            const std::size_t i = padded({x, y});
            pass_[i] = blocked ? kPass | kHasBlock : kPass;
            straight_[i] = static_cast<std::uint16_t>(kStraightCost * 100 / s);
            diagonal_[i] = static_cast<std::uint16_t>(kDiagonalCost * 100 / s);
        }
    }
    std::array<std::ptrdiff_t, 8> offset{};
    for (std::size_t k = 0; k < kNeighbours.size(); ++k) {
        offset[k] = kNeighbours[k].x + static_cast<std::ptrdiff_t>(kNeighbours[k].y) * kPadW;
    }
    const auto unpadded = [](std::size_t p) {
        return SubCell{static_cast<int>(p % kPadW) - 1, static_cast<int>(p / kPadW) - 1};
    };

    // The search runs from the targets outwards, so from a node u it relaxes the neighbours v that
    // step into u, at the cost of that step (no squeezing diagonally past a corner).
    const auto relax_into = [&](std::size_t u, std::int32_t du, auto&& push) {
        for (std::size_t k = 0; k < kNeighbours.size(); ++k) {
            const std::size_t v = u - static_cast<std::size_t>(offset[k]);
            if ((pass_[v] & kPass) == 0) {
                continue;
            }
            const bool diagonal = k >= 4;
            if (diagonal &&
                ((pass_[u - static_cast<std::size_t>(static_cast<std::ptrdiff_t>(kNeighbours[k].y) *
                                                     kPadW)] &
                  kPass) == 0 ||
                 (pass_[u - static_cast<std::size_t>(kNeighbours[k].x)] & kPass) == 0)) {
                continue;
            }
            std::int32_t cost = diagonal ? diagonal_[u] : straight_[u];
            if (in.breach && (pass_[u] & kHasBlock) != 0) {
                const NodeBlocks into = node_blocks(blocks, unpadded(u));
                const NodeBlocks from = node_blocks(blocks, unpadded(v));
                for (int i = 0; i < into.count; ++i) {
                    if (!from.has(into.id[i])) {
                        cost = saturating_add(cost, (*in.break_cost)[into.id[i] - 1u]);
                    }
                }
            }
            const std::int32_t nd = saturating_add(du, cost);
            if (nd < dist_[v]) {
                dist_[v] = nd;
                push(v, nd);
            }
        }
    };
    const auto seed = [&](auto&& push) {
        for (const SubCell t : targets) {
            if (!in_nodes(t)) {
                continue;
            }
            const std::size_t i = padded(t);
            if ((pass_[i] & kPass) != 0 && dist_[i] != 0) {
                dist_[i] = 0;
                push(i, 0);
            }
        }
    };

    if (!in.breach) {
        // Dial's algorithm: a ring of buckets as long as the largest step, each FIFO.
        const std::size_t ring = static_cast<std::size_t>(kDiagonalCost * 100 / slowest) + 1u;
        buckets_.resize(ring);
        for (auto& b : buckets_) {
            b.clear();
        }
        std::size_t pending = 0;
        const auto push = [&](std::size_t v, std::int32_t nd) {
            buckets_[static_cast<std::size_t>(nd) % ring].push_back(static_cast<std::uint32_t>(v));
            ++pending;
        };
        seed(push);
        std::int64_t d = 0;
        while (pending > 0) {
            auto& bucket = buckets_[static_cast<std::size_t>(d) % ring];
            // Every step costs at least 1, so nothing is pushed into the bucket being read.
            for (std::size_t i = 0; i < bucket.size(); ++i) {
                --pending;
                const std::size_t u = bucket[i];
                if (dist_[u] != d) {
                    continue; // a stale entry: it was reached cheaper since
                }
                relax_into(u, static_cast<std::int32_t>(d), push);
            }
            bucket.clear();
            ++d;
        }
        return;
    }

    // The breach field: block costs can be large, so a heap keyed by (cost, node): unique keys,
    // so the order is fixed.
    std::priority_queue<std::uint64_t, std::vector<std::uint64_t>, std::greater<>> heap;
    const auto push = [&](std::size_t v, std::int32_t nd) {
        heap.push((static_cast<std::uint64_t>(static_cast<std::uint32_t>(nd)) << 32u) |
                  static_cast<std::uint64_t>(v));
    };
    seed(push);
    while (!heap.empty()) {
        const std::uint64_t k = heap.top();
        heap.pop();
        const auto du = static_cast<std::int32_t>(k >> 32u);
        const auto u = static_cast<std::size_t>(k & 0xffffffffu);
        if (dist_[u] != du) {
            continue;
        }
        relax_into(u, du, push);
    }
}

std::optional<SubCell> FlowField::next(const FlowInput& in, SubCell n) const noexcept {
    const std::int32_t here = dist(n);
    if (here == kUnreached || here == 0) {
        return std::nullopt;
    }
    std::optional<SubCell> best;
    std::int64_t best_cost = 0;
    for (const SubCell d : kNeighbours) {
        const SubCell m{n.x + d.x, n.y + d.y};
        if (!reached(m) || !can_step(in, n, m)) {
            continue;
        }
        const std::int64_t c = static_cast<std::int64_t>(dist(m)) + step_cost(in, n, m);
        if (!best || c < best_cost) {
            best = m;
            best_cost = c;
        }
    }
    return best;
}

} // namespace pixeltd
