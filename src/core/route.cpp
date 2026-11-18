// Stage 2 of the map generator: the core, the spawn and the path (design/world.md, Path rules).
//
// The route is planned on a coarse lattice of 6x6-cell blocks laid over the map. A seeded
// depth-first search walks the lattice from the core out to a map edge, aiming for the map's
// target length, and never steps next to a block it used before except the one it came from:
// so different stretches of the path stay at least a block apart, and it can't cross itself.
// Blocks the path can't run through, and steps between centres that aren't clear, are never
// taken, so terrain shapes the route. The fine path is the straight lines between block centres,
// a connector from the first block to the core, and a straight run from the last block out to
// the map edge (the spawn). Painting then widens it, keeping a gap between stretches.

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

#include "core/gen_util.hpp"
#include "core/hash.hpp"
#include "core/mapgen.hpp"
#include "core/rng.hpp"

namespace pixeltd {
namespace {

using gen::bfs;
using gen::kDirs;
using gen::rand_range;
using Mask = Grid<std::uint8_t>;

// Rng streams (see stage 1): the map's style is fixed per (seed, spot); attempt k of the route
// uses kStreamRoute + k.
constexpr std::uint64_t kStreamStyle = 0x57a1e000ull;
constexpr std::uint64_t kStreamRoute = 0x40a7e000ull;

// A route within this many cells of its target length is taken straight away.
constexpr int kGoodEnough = 10;

std::uint64_t spot_seed(std::uint64_t seed, Cell spot) {
    const std::uint64_t packed = static_cast<std::uint64_t>(static_cast<std::uint32_t>(spot.y))
                                     << 32u |
                                 static_cast<std::uint64_t>(static_cast<std::uint32_t>(spot.x));
    return splitmix64(seed ^ splitmix64(packed));
}

int chebyshev(Cell a, Cell b) noexcept {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

int chebyshev_to_rect(Cell c, const Rect& r) noexcept {
    const int dx = std::max({r.x - c.x, 0, c.x - (r.x + r.w - 1)});
    const int dy = std::max({r.y - c.y, 0, c.y - (r.y + r.h - 1)});
    return std::max(dx, dy);
}

// --- Where this core's path may run --------------------------------------------------------------

struct PathGround {
    Rect core;
    Mask allowed; // cells the path may cover: ground, plus a high core's ramp cells; never the core
    Mask walk;    // spine cells whose 2-wide brush covers only allowed cells
};

PathGround make_path_ground(const Map& map, Cell spot, const GenParams& p) {
    PathGround g{Rect{spot.x, spot.y, kCoreSize, kCoreSize}, Mask(kMapW, kMapH, 0),
                 Mask(kMapW, kMapH, 0)};
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const bool ground = map.terrain.at(x, y) == Terrain::Ground;
            g.allowed.at(x, y) = ground && !g.core.contains(Cell{x, y}) ? 1 : 0;
        }
    }
    if (map.terrain.at(spot) == Terrain::High) {
        // The ramp: high cells of the core's own plateau within ramp_max cells of the core
        // (the same rule stage 1 validated with).
        const int r = p.ramp_max;
        const Rect region{spot.x - r, spot.y - r, kCoreSize + 2 * r, kCoreSize + 2 * r};
        Mask seen(kMapW, kMapH, 0);
        std::vector<Cell> queue{spot};
        seen.at(spot) = 1;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const Cell c = queue[head];
            if (!g.core.contains(c)) {
                g.allowed.at(c) = 1;
            }
            for (const Cell d : kDirs) {
                const Cell n{c.x + d.x, c.y + d.y};
                if (region.contains(n) && map.terrain.in_bounds(n) && seen.at(n) == 0 &&
                    map.terrain.at(n) == Terrain::High) {
                    seen.at(n) = 1;
                    queue.push_back(n);
                }
            }
        }
    }
    for (int y = 0; y + 1 < kMapH; ++y) {
        for (int x = 0; x + 1 < kMapW; ++x) {
            g.walk.at(x, y) = g.allowed.at(x, y) != 0 && g.allowed.at(x + 1, y) != 0 &&
                                      g.allowed.at(x, y + 1) != 0 && g.allowed.at(x + 1, y + 1) != 0
                                  ? 1
                                  : 0;
        }
    }
    return g;
}

// --- The coarse lattice
// ---------------------------------------------------------------------------

constexpr int kBlock = 6;
constexpr int kLatW = kMapW / kBlock; // 26 blocks across
constexpr int kLatH = kMapH / kBlock; // 10 down
constexpr int kLatCount = kLatW * kLatH;
// The lattice is centred on the map: block (0, 0) starts at this cell.
constexpr int kLatX = (kMapW - kLatW * kBlock) / 2;
constexpr int kLatY = (kMapH - kLatH * kBlock) / 2;
// How far a link between two neighbouring blocks may stray outside them.
constexpr int kCorridorPad = 2;

// A block's cells. Anchors stay inside their block and links inside the two blocks they join;
// the search never lets stretches far apart along the path use neighbouring blocks (even
// diagonally), so they stay at least a block apart.
constexpr Rect inner_of(int node) noexcept {
    return {kLatX + node % kLatW * kBlock, kLatY + node / kLatW * kBlock, kBlock, kBlock};
}

// The block next to `node` in direction d (kDirs order: right, left, down, up), or -1.
constexpr int neighbour(int node, int d) noexcept {
    const int x = node % kLatW + kDirs[static_cast<std::size_t>(d)].x;
    const int y = node / kLatW + kDirs[static_cast<std::size_t>(d)].y;
    return x >= 0 && y >= 0 && x < kLatW && y < kLatH ? y * kLatW + x : -1;
}

// Removes loops so no cell appears twice: where the path comes back to a cell it has already
// visited, the stretch in between is cut out. Neighbouring links can share a few cells beside an
// anchor (anchors aren't aligned, so links can be L-shaped), and so can the connector and the
// first link; this keeps the spine a simple path. It stays connected, and only gets shorter.
// `index` is scratch space: all -1 on entry, and left that way.
void erase_loops(std::vector<Cell>& spine, Grid<int>& index, std::vector<Cell>& out) {
    out.clear();
    for (const Cell c : spine) {
        const int seen = index.at(c);
        if (seen >= 0) {
            for (std::size_t i = static_cast<std::size_t>(seen) + 1; i < out.size(); ++i) {
                index.at(out[i]) = -1;
            }
            out.resize(static_cast<std::size_t>(seen) + 1);
            continue;
        }
        index.at(c) = static_cast<int>(out.size());
        out.push_back(c);
    }
    for (const Cell c : out) {
        index.at(c) = -1;
    }
    spine.swap(out);
}

// How far along the path a hook can be cut out by tighten().
constexpr std::size_t kHookWindow = 48;

// One pass of cutting hooks: where the spine comes back within 2 cells of itself (at a turn the
// link in can run into a block's anchor and the link out leave beside it), the stretch in
// between is replaced by the shortest bridge of walkable cells nearby that avoids the rest of the
// path. True if anything changed.
bool cut_hooks(std::vector<Cell>& spine, const Mask& walk, Grid<int>& index,
               std::vector<Cell>& out) {
    const std::size_t n = spine.size();
    for (std::size_t k = 0; k < n; ++k) {
        index.at(spine[k]) = static_cast<int>(k);
    }
    out.clear();
    bool changed = false;
    constexpr int kBox = 7; // the bridge search stays within 2 cells of the two ends
    std::array<int, kBox * kBox> from{};
    std::array<int, kBox * kBox> queue{};
    std::array<Cell, kBox * kBox> bridge{};
    for (std::size_t i = 0; i < n;) {
        out.push_back(spine[i]);
        std::size_t to = 0;
        int bridge_len = 0;
        for (std::size_t j = std::min(n - 1, i + kHookWindow); j > i + 3 && to == 0; --j) {
            const Cell a = spine[i];
            const Cell b = spine[j];
            if (std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)) > 2) {
                continue;
            }
            // Breadth-first search from a to b inside a small box, over walkable cells the path
            // doesn't keep (cells of the stretch being cut are fine).
            const Cell box{std::min(a.x, b.x) - 2, std::min(a.y, b.y) - 2};
            const auto slot = [&](Cell c) { return (c.y - box.y) * kBox + (c.x - box.x); };
            const auto in_box = [&](Cell c) {
                return c.x >= box.x && c.y >= box.y && c.x < box.x + kBox && c.y < box.y + kBox;
            };
            from.fill(-1);
            int head = 0;
            int tail = 0;
            queue[static_cast<std::size_t>(tail++)] = slot(a);
            from[static_cast<std::size_t>(slot(a))] = slot(a);
            bool reached = false;
            while (head < tail && !reached) {
                const int s = queue[static_cast<std::size_t>(head++)];
                const Cell c{box.x + s % kBox, box.y + s / kBox};
                for (const Cell d : kDirs) {
                    const Cell nb{c.x + d.x, c.y + d.y};
                    if (!in_box(nb) || from[static_cast<std::size_t>(slot(nb))] >= 0) {
                        continue;
                    }
                    if (!(nb == b)) {
                        if (!walk.in_bounds(nb) || walk.at(nb) == 0) {
                            continue;
                        }
                        const int at = index.at(nb);
                        if (at >= 0 && (at <= static_cast<int>(i) || at >= static_cast<int>(j))) {
                            continue; // a cell the path keeps
                        }
                    }
                    from[static_cast<std::size_t>(slot(nb))] = s;
                    queue[static_cast<std::size_t>(tail++)] = slot(nb);
                    reached = reached || nb == b;
                }
            }
            if (!reached) {
                continue;
            }
            int len = 0;
            for (int s = from[static_cast<std::size_t>(slot(b))]; s != slot(a);
                 s = from[static_cast<std::size_t>(s)]) {
                bridge[static_cast<std::size_t>(len++)] = Cell{box.x + s % kBox, box.y + s / kBox};
            }
            if (static_cast<std::size_t>(len) + 1 < j - i) {
                std::reverse(bridge.begin(), bridge.begin() + len);
                to = j;
                bridge_len = len;
            }
        }
        if (to == 0) {
            ++i;
            continue;
        }
        out.insert(out.end(), bridge.begin(), bridge.begin() + bridge_len);
        i = to;
        changed = true;
    }
    for (const Cell c : spine) {
        index.at(c) = -1;
    }
    spine.swap(out);
    return changed;
}

// True if stretches of the spine that are far apart along it (params.stretch_span cells or more)
// stay at least params.stretch_gap cells apart on the map. `index` is scratch space: all -1 on
// entry, and left that way.
bool stretches_apart(const std::vector<Cell>& spine, const GenParams& p, Grid<int>& index) {
    for (std::size_t k = 0; k < spine.size(); ++k) {
        index.at(spine[k]) = static_cast<int>(k);
    }
    const int r = p.stretch_gap - 1;
    bool ok = true;
    for (std::size_t k = 0; k < spine.size() && ok; ++k) {
        const Cell c = spine[k];
        for (int y = c.y - r; y <= c.y + r && ok; ++y) {
            for (int x = c.x - r; x <= c.x + r; ++x) {
                if (index.in_bounds(x, y) &&
                    index.at(x, y) >= static_cast<int>(k) + p.stretch_span) {
                    ok = false;
                    break;
                }
            }
        }
    }
    for (const Cell c : spine) {
        index.at(c) = -1;
    }
    return ok;
}

// Makes the assembled spine a clean simple path: loops erased, hooks cut. It stays connected and
// only gets shorter. `index` is scratch space: all -1 on entry, and left that way.
void tighten(std::vector<Cell>& spine, const Mask& walk, Grid<int>& index, std::vector<Cell>& out) {
    erase_loops(spine, index, out);
    for (int pass = 0; pass < 4 && cut_hooks(spine, walk, index, out); ++pass) {
        erase_loops(spine, index, out);
    }
}

// The lattice for one core: each block's anchor (the walkable cell nearest its centre, inside its
// inner area) and the links between neighbouring anchors (the shortest walkable route through
// the corridor joining their inner areas). The same for every attempt, so it's built once.
struct Lattice {
    std::array<std::optional<Cell>, kLatCount> anchor;
    std::array<std::optional<std::vector<Cell>>, kLatCount> right; // cells strictly between
    std::array<std::optional<std::vector<Cell>>, kLatCount> down;

    Lattice(const Mask& walk, const Grid<int>& to_core) {
        const auto usable = [&](Cell c) {
            return walk.in_bounds(c) && walk.at(c) != 0 && to_core.at(c) >= 0;
        };
        for (int n = 0; n < kLatCount; ++n) {
            const Rect in = inner_of(n);
            const Cell mid{in.x + in.w / 2, in.y + in.h / 2};
            auto& slot = anchor[static_cast<std::size_t>(n)];
            for (int r = 0; r < kBlock && !slot; ++r) {
                for (int dy = -r; dy <= r && !slot; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        const Cell c{mid.x + dx, mid.y + dy};
                        if (std::max(std::abs(dx), std::abs(dy)) == r && in.contains(c) &&
                            usable(c)) {
                            slot = c;
                            break;
                        }
                    }
                }
            }
        }
        Grid<int> from(kMapW, kMapH, -1);
        std::vector<Cell> queue;
        std::vector<Cell> touched;
        for (int n = 0; n < kLatCount; ++n) {
            for (int d = 0; d < 4; d += 2) { // right (0) and down (2)
                const int m = neighbour(n, d);
                const auto& a = anchor[static_cast<std::size_t>(n)];
                if (m < 0 || !a || !anchor[static_cast<std::size_t>(m)]) {
                    continue;
                }
                const Cell b = *anchor[static_cast<std::size_t>(m)];
                const Rect u = inner_of(n);
                const Rect v = inner_of(m);
                // The two blocks plus a margin, so a link can squeeze through a narrow gap
                // that runs along their border.
                const Rect corridor{u.x - kCorridorPad, u.y - kCorridorPad,
                                    v.x + v.w - u.x + 2 * kCorridorPad,
                                    v.y + v.h - u.y + 2 * kCorridorPad};
                // Breadth-first search from a to b inside the corridor.
                queue.assign(1, *a);
                touched.assign(1, *a);
                from.at(*a) = a->y * kMapW + a->x;
                bool found = false;
                for (std::size_t head = 0; head < queue.size() && !found; ++head) {
                    const Cell c = queue[head];
                    for (const Cell dir : kDirs) {
                        const Cell nb{c.x + dir.x, c.y + dir.y};
                        if (corridor.contains(nb) && usable(nb) && from.at(nb) < 0) {
                            from.at(nb) = c.y * kMapW + c.x;
                            touched.push_back(nb);
                            queue.push_back(nb);
                            found = found || nb == b;
                        }
                    }
                }
                if (found) {
                    std::vector<Cell> between;
                    for (Cell c = b;;) {
                        const int f = from.at(c);
                        c = Cell{f % kMapW, f / kMapW};
                        if (c == *a) {
                            break;
                        }
                        between.push_back(c);
                    }
                    std::reverse(between.begin(), between.end());
                    (d == 0 ? right : down)[static_cast<std::size_t>(n)] = std::move(between);
                }
                for (const Cell c : touched) {
                    from.at(c) = -1;
                }
            }
        }
    }

    // The link from block u to its neighbour v (cells strictly between their anchors, stored
    // from the left or upper block), or nullptr if they aren't linked.
    [[nodiscard]] const std::vector<Cell>* link(int u, int v) const {
        const auto& l = v == u + 1       ? right[static_cast<std::size_t>(u)]
                        : v == u - 1     ? right[static_cast<std::size_t>(v)]
                        : v == u + kLatW ? down[static_cast<std::size_t>(u)]
                                         : down[static_cast<std::size_t>(v)];
        return l ? &*l : nullptr;
    }
    [[nodiscard]] bool linked(int u, int v) const { return link(u, v) != nullptr; }

    // Appends the link's cells in order from u to v.
    void append_link(int u, int v, std::vector<Cell>& out) const {
        const std::vector<Cell>& l = *link(u, v);
        if (v == u + 1 || v == u + kLatW) {
            out.insert(out.end(), l.begin(), l.end());
        } else {
            out.insert(out.end(), l.rbegin(), l.rend());
        }
    }
};

// One route attempt: a depth-first search over the lattice.
class Planner {
  public:
    Planner(const Lattice& lattice, const PathGround& ground, const Grid<int>& to_core,
            const GenParams& p, Rng& rng, int target)
        : lat_(lattice), ground_(ground), to_core_(to_core), p_(p), rng_(rng),
          // Cutting hooks after the search shortens a route a little: leave room for it.
          floor_(p.path_min_len + kTightenRoom), target_(std::max(target, floor_)) {}

    // Plans a route; fills `spine` from the spawn to the core. False if none was found.
    bool plan(std::vector<Cell>& spine) {
        // Start blocks, nearest the core (by path) first. The seed picks one of the nearest three
        // to begin with; if no route fits from it, the others are tried in order of distance.
        std::vector<std::pair<int, int>> near; // (cells to the core, block)
        for (int n = 0; n < kLatCount; ++n) {
            if (lat_.anchor[static_cast<std::size_t>(n)]) {
                near.emplace_back(to_core_.at(anchor(n)), n);
            }
        }
        std::sort(near.begin(), near.end());
        const std::size_t tries = std::min<std::size_t>(near.size(), kStarts);
        if (tries == 0) {
            return false;
        }
        const std::size_t first =
            rng_.next_below(static_cast<std::uint32_t>(std::min<std::size_t>(tries, 3)));
        for (std::size_t k = 0; k < tries; ++k) {
            const std::size_t i = k == 0 ? first : (k - 1 < first ? k - 1 : k);
            set_up_start(near[i].second);
            if (!find_exits()) {
                continue;
            }
            on_path_.fill(0);
            on_path_[static_cast<std::size_t>(start_)] = 1;
            path_.assign(1, start_);
            best_.clear();
            best_gap_ = INT_MAX;
            expansions_ = 0;
            done_ = false;
            search(start_, -1, static_cast<int>(connector_.size()) + 1);
            if (!best_.empty()) {
                // Cutting hooks shortens the route a little more; if that takes it below the
                // minimum, try the next start.
                assemble(best_, spine);
                tighten(spine, ground_.walk, loop_index_, loop_out_);
                if (static_cast<int>(spine.size()) >= p_.path_min_len &&
                    stretches_apart(spine, p_, loop_index_)) {
                    return true;
                }
            }
        }
        return false;
    }

  private:
    bool walkable(Cell c) const { return ground_.walk.in_bounds(c) && ground_.walk.at(c) != 0; }
    Cell anchor(int n) const { return *lat_.anchor[static_cast<std::size_t>(n)]; }
    bool usable(int n) const { return usable_[static_cast<std::size_t>(n)] != 0; }
    bool on_path(int n) const { return n >= 0 && on_path_[static_cast<std::size_t>(n)] != 0; }

    // The first block, the connector from its anchor to the core, and which blocks the route
    // may use.
    void set_up_start(int block) {
        start_ = block;
        // The connector: down the distance field from the anchor to the core.
        connector_.clear();
        Cell c = anchor(start_);
        while (to_core_.at(c) > 0) {
            for (const Cell d : kDirs) {
                const Cell n{c.x + d.x, c.y + d.y};
                if (to_core_.in_bounds(n) && to_core_.at(n) == to_core_.at(c) - 1 && walkable(n)) {
                    c = n;
                    break;
                }
            }
            connector_.push_back(c);
        }
        // Blocks the route may use (anchored), and which of them are close to the core or the
        // connector: those are only allowed in the first steps away from the core, so the route
        // can leave but never wraps back round it.
        for (int n = 0; n < kLatCount; ++n) {
            const bool anchored = lat_.anchor[static_cast<std::size_t>(n)].has_value();
            bool close = false;
            if (anchored && n != start_) {
                const Cell a = anchor(n);
                close = chebyshev_to_rect(a, ground_.core) <= p_.separation;
                for (const Cell k : connector_) {
                    close = close || chebyshev(a, k) <= p_.separation;
                }
            }
            usable_[static_cast<std::size_t>(n)] = anchored ? 1 : 0;
            near_core_[static_cast<std::size_t>(n)] = close ? 1 : 0;
        }
    }

    // Exits: rim blocks from whose anchor a short walk (inside the block and out to the map's
    // edge beside it) reaches an edge cell at least min_spawn_dist from the core by path. The
    // walk becomes the start of the path. Then the lattice distance from every block to the
    // nearest exit, for pruning.
    bool find_exits() {
        bool any = false;
        Grid<int> from(kMapW, kMapH, -1);
        std::vector<Cell> queue;
        for (int n = 0; n < kLatCount; ++n) {
            exit_path_[static_cast<std::size_t>(n)].reset();
            const int lx = n % kLatW;
            const int ly = n / kLatW;
            const bool left = lx == 0;
            const bool right = lx == kLatW - 1;
            const bool top = ly == 0;
            const bool bottom = ly == kLatH - 1;
            if (!usable(n) || !(left || right || top || bottom)) {
                continue;
            }
            const Rect b = inner_of(n);
            const int x0 = left ? 0 : b.x;
            const int y0 = top ? 0 : b.y;
            const int x1 = right ? kMapW - 2 : b.x + b.w - 1;
            const int y1 = bottom ? kMapH - 2 : b.y + b.h - 1;
            const Rect region{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
            const auto is_exit = [&](Cell c) {
                const bool on_edge = (left && c.x == 0) || (right && c.x == kMapW - 2) ||
                                     (top && c.y == 0) || (bottom && c.y == kMapH - 2);
                return on_edge && to_core_.at(c) >= p_.min_spawn_dist;
            };
            const Cell a = anchor(n);
            queue.assign(1, a);
            from.at(a) = a.y * kMapW + a.x;
            std::optional<Cell> edge;
            for (std::size_t head = 0; head < queue.size() && !edge; ++head) {
                const Cell c = queue[head];
                if (is_exit(c)) {
                    edge = c;
                    break;
                }
                for (const Cell d : kDirs) {
                    const Cell nb{c.x + d.x, c.y + d.y};
                    if (region.contains(nb) && walkable(nb) && from.at(nb) < 0) {
                        from.at(nb) = c.y * kMapW + c.x;
                        queue.push_back(nb);
                    }
                }
            }
            if (edge) {
                std::vector<Cell> walk_out; // edge first, up to the cell before the anchor
                for (Cell c = *edge; !(c == a);) {
                    walk_out.push_back(c);
                    const int f = from.at(c);
                    c = Cell{f % kMapW, f / kMapW};
                }
                exit_path_[static_cast<std::size_t>(n)] = std::move(walk_out);
                any = true;
            }
            for (const Cell c : queue) {
                from.at(c) = -1;
            }
        }
        to_exit_.fill(-1);
        std::vector<int> blocks;
        for (int n = 0; n < kLatCount; ++n) {
            if (exit_path_[static_cast<std::size_t>(n)]) {
                to_exit_[static_cast<std::size_t>(n)] = 0;
                blocks.push_back(n);
            }
        }
        for (std::size_t head = 0; head < blocks.size(); ++head) {
            const int u = blocks[head];
            for (int d = 0; d < 4; ++d) {
                const int v = neighbour(u, d);
                if (v >= 0 && usable(v) && to_exit_[static_cast<std::size_t>(v)] < 0 &&
                    lat_.linked(u, v)) {
                    to_exit_[static_cast<std::size_t>(v)] =
                        to_exit_[static_cast<std::size_t>(u)] + 1;
                    blocks.push_back(v);
                }
            }
        }
        return any;
    }

    // len: spine cells from u's anchor to the core, inclusive.
    void search(int u, int dir_in, int len) {
        if (done_ || ++expansions_ > kBudget) {
            done_ = true;
            return;
        }
        if (const auto& out = exit_path_[static_cast<std::size_t>(u)]) {
            // Links beside an anchor can overlap, and those loops get cut, so the real length is
            // at most this estimate. Only measure it when it could be in range and beat the best.
            const int estimate = len + static_cast<int>(out->size());
            int total = -1;
            if (estimate >= floor_ && estimate <= p_.path_max_len + kLoopSlack &&
                (best_gap_ == INT_MAX || estimate - target_ < best_gap_ + kLoopSlack)) {
                assemble(path_, candidate_);
                erase_loops(candidate_, loop_index_, loop_out_);
                total = static_cast<int>(candidate_.size());
            }
            if (total >= floor_ && total <= p_.path_max_len &&
                std::abs(total - target_) < best_gap_) {
                best_ = path_;
                best_gap_ = std::abs(total - target_);
                if (best_gap_ <= kGoodEnough) {
                    done_ = true;
                    return;
                }
            }
        }
        // Prune: no exit is reachable within the maximum length any more (a link adds at least
        // kMinLink cells, so this never prunes a route that could still fit).
        const int home = to_exit_[static_cast<std::size_t>(u)];
        if (home < 0 || len + kMinLink * home > p_.path_max_len) {
            return;
        }
        // Candidate steps, best first. With plenty of length still wanted the route roams (steps
        // that don't head for an exit first); near the target it heads home. Straight steps come
        // before turns, so the path keeps runs; ties are broken by the seed.
        struct Step {
            int node;
            int dir;
            int key;
            std::uint32_t tie;
        };
        std::array<Step, 4> steps{};
        int count = 0;
        const int need = target_ - len - kBlock;
        const int prev = path_.size() >= 2 ? path_[path_.size() - 2] : -1;
        for (int d = 0; d < 4; ++d) {
            const int v = neighbour(u, d);
            if (v < 0 || !usable(v) || on_path(v) || to_exit_[static_cast<std::size_t>(v)] < 0 ||
                (near_core_[static_cast<std::size_t>(v)] != 0 && path_.size() > kLeaveSteps) ||
                !lat_.linked(u, v) || touches_path(v, u, prev)) {
                continue;
            }
            const int v_home = to_exit_[static_cast<std::size_t>(v)];
            const bool roam = need > kBlock * v_home + 3 * kBlock;
            const bool closer = v_home < home;
            const int key = (roam == closer ? 2 : 0) + (d == dir_in ? 0 : 1);
            steps[static_cast<std::size_t>(count++)] = {v, d, key, rng_.next_u32()};
        }
        // Best first: an insertion sort over at most 4 steps (std::sort over a part of the array
        // trips GCC's array-bounds analysis).
        const auto before = [](const Step& a, const Step& b) {
            return a.key != b.key   ? a.key < b.key
                   : a.tie != b.tie ? a.tie < b.tie
                                    : a.node < b.node;
        };
        const std::size_t n = static_cast<std::size_t>(std::min(count, 4));
        for (std::size_t i = 1; i < n; ++i) {
            const Step s = steps[i];
            std::size_t j = i;
            while (j > 0 && before(s, steps[j - 1])) {
                steps[j] = steps[j - 1];
                --j;
            }
            steps[j] = s;
        }
        for (int i = 0; i < count && !done_; ++i) {
            const Step& s = steps[static_cast<std::size_t>(i)];
            const int step_len = static_cast<int>(lat_.link(u, s.node)->size()) + 1;
            on_path_[static_cast<std::size_t>(s.node)] = 1;
            path_.push_back(s.node);
            search(s.node, s.dir, len + step_len);
            path_.pop_back();
            on_path_[static_cast<std::size_t>(s.node)] = 0;
        }
    }

    // True if block v would come next to (even diagonally) a block already on the path other than
    // u and the block before u (that's just a turn): separate stretches stay a block apart.
    bool touches_path(int v, int u, int prev) const {
        const int vx = v % kLatW;
        const int vy = v / kLatW;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int wx = vx + dx;
                const int wy = vy + dy;
                if ((dx == 0 && dy == 0) || wx < 0 || wy < 0 || wx >= kLatW || wy >= kLatH) {
                    continue;
                }
                const int w = wy * kLatW + wx;
                if (w != u && w != prev && on_path(w)) {
                    return true;
                }
            }
        }
        return false;
    }

    // The fine spine, spawn to core: the run in from the edge, the anchors and the links between
    // them, then the connector.
    void assemble(const std::vector<int>& blocks, std::vector<Cell>& spine) const {
        spine.clear();
        const int exit = blocks.back();
        const std::vector<Cell>& walk_out = *exit_path_[static_cast<std::size_t>(exit)];
        spine.insert(spine.end(), walk_out.begin(), walk_out.end());
        for (std::size_t i = blocks.size(); i-- > 0;) {
            spine.push_back(anchor(blocks[i]));
            if (i > 0) {
                lat_.append_link(blocks[i], blocks[i - 1], spine);
            }
        }
        spine.insert(spine.end(), connector_.begin(), connector_.end());
    }

    static constexpr int kBudget = 8000;    // search steps per start
    static constexpr int kMinLink = 1;      // the fewest cells a step between neighbours can add
    static constexpr int kLoopSlack = 40;   // most a route can lose to cut loops, for the estimate
    static constexpr int kTightenRoom = 16; // headroom over the minimum for cutting hooks
    static constexpr std::size_t kLeaveSteps = 4; // steps that may stay close to the core
    static constexpr std::size_t kStarts = 12;    // start blocks tried per attempt

    const Lattice& lat_;
    const PathGround& ground_;
    const Grid<int>& to_core_;
    const GenParams& p_;
    Rng& rng_;
    int floor_; // the shortest route the search accepts
    int target_;

    int start_ = 0;
    std::vector<Cell> connector_; // after the first anchor, ending beside the core
    std::array<std::uint8_t, kLatCount> usable_{};
    std::array<std::uint8_t, kLatCount> near_core_{};
    std::array<std::optional<std::vector<Cell>>, kLatCount> exit_path_{}; // edge to anchor
    std::array<int, kLatCount> to_exit_{};
    std::array<std::uint8_t, kLatCount> on_path_{};
    std::vector<int> path_;
    std::vector<int> best_;
    int best_gap_ = 0;
    int expansions_ = 0;
    bool done_ = false;
    std::vector<Cell> candidate_; // the route being measured at an exit
    Grid<int> loop_index_{kMapW, kMapH, -1};
    std::vector<Cell> loop_out_;
};

// --- Widths and painting ------------------------------------------------------------------------

// The planned path width at each spine cell: the base, with wide stretches and chokepoints,
// changing by at most 1 every 2 cells. paint_path fits it to the ground.
std::vector<int> plan_widths(int n, int base, Rng& rng, const GenParams& p) {
    std::vector<int> w(static_cast<std::size_t>(n), base);
    const auto stretch = [&](int len_min, int len_max, int width) {
        const int len = rand_range(rng, len_min, len_max);
        if (n - 20 - len < 10) {
            return;
        }
        const int start = rand_range(rng, 10, n - 10 - len);
        for (int i = start; i < start + len; ++i) {
            w[static_cast<std::size_t>(i)] = width;
        }
    };
    const int wides = rand_range(rng, p.wides_min, p.wides_max);
    for (int i = 0; i < wides; ++i) {
        const int lo = std::min(base + 1, p.wide_width_max);
        stretch(p.wide_len_min, p.wide_len_max, rand_range(rng, lo, p.wide_width_max));
    }
    const int chokes = rand_range(rng, p.chokes_min, p.chokes_max);
    for (int i = 0; i < chokes; ++i) {
        stretch(p.choke_len_min, p.choke_len_max, 2);
    }
    // Gentle transitions: at most +1 per 2 cells, both ways along the path.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 1; i < n; ++i) {
            const auto at = [&](int j) -> int& {
                return w[static_cast<std::size_t>(pass == 0 ? j : n - 1 - j)];
            };
            at(i) = std::min(at(i), at(i - 1) + 1);
            if (i >= 2) {
                at(i) = std::min(at(i), at(i - 2) + 1);
            }
        }
    }
    return w;
}

// Spine cells closer than this along the path are the same stretch: their brushes may overlap.
constexpr int kSameStretch = 16;

// Paints map.path from map.spine and sets map.widths. Every spine cell is painted at width 2
// first (it always fits, and the router keeps stretches apart). Then each one widens towards its
// planned width while the wider brush stays on allowed ground (off-map is fine: the path runs on
// past the edge) and keeps a 1-cell gap from every other stretch, so stretches never merge.
void paint_path(Map& map, const Mask& allowed, const std::vector<int>& planned) {
    const int n = static_cast<int>(map.spine.size());
    map.path.fill(PathKind::None);
    map.widths.assign(map.spine.size(), 2);
    Grid<int> owner(kMapW, kMapH, -1); // the first spine index that painted each cell
    const auto paint = [&](int i, int width) {
        const Cell c = map.spine[static_cast<std::size_t>(i)];
        const int back = (width - 1) / 2;
        for (int y = c.y - back; y < c.y - back + width; ++y) {
            for (int x = c.x - back; x < c.x - back + width; ++x) {
                if (owner.in_bounds(x, y) && owner.at(x, y) < 0) {
                    owner.at(x, y) = i;
                    map.path.at(x, y) =
                        map.terrain.at(x, y) == Terrain::High ? PathKind::Ramp : PathKind::Path;
                }
            }
        }
    };
    const auto other_stretch = [&](int x, int y, int i) {
        const int o = owner.in_bounds(x, y) ? owner.at(x, y) : -1;
        return o >= 0 && std::abs(o - i) > kSameStretch;
    };
    const auto widen_ok = [&](int i, int width) {
        const Cell c = map.spine[static_cast<std::size_t>(i)];
        const int back = (width - 1) / 2;
        for (int y = c.y - back - 1; y <= c.y - back + width; ++y) {
            for (int x = c.x - back - 1; x <= c.x - back + width; ++x) {
                const bool inside = x >= c.x - back && x < c.x - back + width && y >= c.y - back &&
                                    y < c.y - back + width;
                if (other_stretch(x, y, i)) {
                    return false; // overlaps or touches another stretch
                }
                if (inside && allowed.in_bounds(x, y) && allowed.at(x, y) == 0) {
                    return false;
                }
            }
        }
        return true;
    };
    for (int i = 0; i < n; ++i) {
        paint(i, 2);
    }
    for (int i = 0; i < n; ++i) {
        for (int width = planned[static_cast<std::size_t>(i)]; width > 2; --width) {
            if (widen_ok(i, width)) {
                paint(i, width);
                map.widths[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(width);
                break;
            }
        }
    }
}

void reset_stage2(Map& map) {
    map.core.reset();
    map.spawn = {};
    map.spine.clear();
    map.widths.clear();
    map.path.fill(PathKind::None);
    map.base_width = 0;
    map.winding = 0;
    map.target_length = 0;
    map.stats.stage2_attempts = 0;
    map.stats.stage2_fallback = false;
}

} // namespace

std::optional<Cell> central_core_spot(const Map& map) {
    std::optional<Cell> best;
    long long best_d = 0;
    // Distances in doubled units, so the zone's and the core's centres are whole numbers.
    const int zx = 2 * map.zone.x + map.zone.w;
    const int zy = 2 * map.zone.y + map.zone.h;
    for (const Cell s : legal_core_spots(map)) {
        const long long dx = 2 * s.x + kCoreSize - zx;
        const long long dy = 2 * s.y + kCoreSize - zy;
        const long long d = dx * dx + dy * dy;
        if (!best || d < best_d) {
            best = s;
            best_d = d;
        }
    }
    return best;
}

bool is_light_spot(const Map& map, Cell t) noexcept {
    if (!map.terrain.in_bounds(t) || !map.terrain.in_bounds(t.x + 1, t.y + 1)) {
        return false;
    }
    const Terrain first = map.terrain.at(t);
    if (first != Terrain::Ground && first != Terrain::High) {
        return false;
    }
    const Rect core = map.core ? Rect{map.core->x, map.core->y, kCoreSize, kCoreSize} : Rect{};
    for (int y = t.y; y < t.y + kLightSize; ++y) {
        for (int x = t.x; x < t.x + kLightSize; ++x) {
            if (map.terrain.at(x, y) != first || map.path.at(x, y) != PathKind::None ||
                core.contains(Cell{x, y})) {
                return false;
            }
        }
    }
    return true;
}

bool build_space_ok(const Map& map, const GenParams& p) {
    const int n = static_cast<int>(map.spine.size());
    if (n == 0) {
        return false;
    }
    const int r = p.light_range_cells;
    // Centres in half cells: a spine cell's is (2x+1, 2y+1); a 2x2 spot's is (2x+2, 2y+2).
    const long long reach = 4LL * r * r;
    for (int start = 0; start < n; start += p.build_window) {
        bool found = false;
        for (int i = start; i < std::min(n, start + p.build_window) && !found; ++i) {
            const Cell s = map.spine[static_cast<std::size_t>(i)];
            for (int ty = s.y - r - 1; ty <= s.y + r && !found; ++ty) {
                for (int tx = s.x - r - 1; tx <= s.x + r && !found; ++tx) {
                    const long long dx = 2LL * tx + 2 - (2LL * s.x + 1);
                    const long long dy = 2LL * ty + 2 - (2LL * s.y + 1);
                    found = dx * dx + dy * dy <= reach && is_light_spot(map, {tx, ty});
                }
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool generate_stage2(Map& map, Cell spot, const GenParams& p) {
    if (!is_legal_core_spot(map, spot)) {
        return false;
    }
    reset_stage2(map);
    map.core = spot;

    const PathGround ground = make_path_ground(map, spot, p);
    std::vector<Cell> access;
    core_access_cells(spot, access);
    std::vector<Cell> goal;
    for (const Cell c : access) {
        if (ground.walk.in_bounds(c) && ground.walk.at(c) != 0) {
            goal.push_back(c);
        }
    }
    // Steps from every cell to the core over 2-wide brush cells; -1 = can't reach it.
    Grid<int> to_core(kMapW, kMapH, -1);
    bfs(to_core, goal, [&](Cell c) { return ground.walk.at(c) != 0; });

    // The map's style, fixed for this (seed, spot).
    const std::uint64_t base_seed = spot_seed(map.seed, spot);
    Rng style(base_seed, kStreamStyle);
    map.base_width = rand_range(style, p.base_width_min, p.base_width_max);
    map.winding = rand_range(style, 0, 100);
    map.target_length = p.path_min_len + (p.path_max_len - p.path_min_len) * map.winding / 100;

    const Lattice lattice(ground.walk, to_core);
    std::vector<Cell> spine;
    for (int attempt = 0; attempt < p.stage2_attempts; ++attempt) {
        Rng rng(base_seed, kStreamRoute + static_cast<std::uint64_t>(attempt));
        Planner planner(lattice, ground, to_core, p, rng, map.target_length);
        if (!planner.plan(spine)) {
            continue;
        }
        map.spawn = spine.front();
        map.spine = spine;
        paint_path(map, ground.allowed,
                   plan_widths(static_cast<int>(spine.size()), map.base_width, rng, p));
        if (!build_space_ok(map, p)) {
            map.path.fill(PathKind::None);
            continue;
        }
        map.stats.stage2_attempts = attempt + 1;
        return true;
    }

    // Fallback: the plain shortest route from the farthest reachable edge cell, at width 2.
    std::optional<Cell> farthest;
    for (int y = 0; y + 1 < kMapH; ++y) {
        for (int x = 0; x + 1 < kMapW; ++x) {
            const bool edge = x == 0 || y == 0 || x == kMapW - 2 || y == kMapH - 2;
            if (edge && to_core.at(x, y) >= 0 &&
                (!farthest || to_core.at(x, y) > to_core.at(*farthest))) {
                farthest = Cell{x, y};
            }
        }
    }
    map.stats.stage2_attempts = p.stage2_attempts;
    map.stats.stage2_fallback = true;
    if (!farthest) {
        return true; // stage 1 rules this out; the tests check it
    }
    spine.assign(1, *farthest);
    for (Cell c = *farthest; to_core.at(c) > 0;) {
        for (const Cell d : kDirs) {
            const Cell n{c.x + d.x, c.y + d.y};
            if (to_core.in_bounds(n) && to_core.at(n) == to_core.at(c) - 1) {
                c = n;
                break;
            }
        }
        spine.push_back(c);
    }
    map.spawn = spine.front();
    map.spine = spine;
    paint_path(map, ground.allowed, std::vector<int>(spine.size(), 2));
    return true;
}

} // namespace pixeltd
