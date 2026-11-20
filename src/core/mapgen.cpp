#include "core/mapgen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "core/gen_util.hpp"
#include "core/hash.hpp"
#include "core/rng.hpp"
#include "core/terrain.hpp"

namespace pixeltd {
namespace {

using gen::fill_noise;
using gen::kDirs;
using gen::rand_range;
using Mask = Grid<std::uint8_t>;

// Attempt k uses Rng stream kStreamTerrain + k, from splitmix64(seed).
constexpr std::uint64_t kStreamTerrain = 0x7e11a100ull;

constexpr int kCellCount = kMapW * kMapH;

constexpr std::size_t index_of(int x, int y) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(kMapW) +
           static_cast<std::size_t>(x);
}

template <class T> void shuffle(std::vector<T>& v, Rng& rng) {
    for (std::size_t i = v.size(); i > 1; --i) {
        const std::size_t j = rng.next_below(static_cast<std::uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

// A noise layer's 0..255 value in every cell.
void noise255(Grid<int>& field, Rng& rng, const NoiseSpec& n) {
    const int total = std::max(1, fill_noise(field, rng, n));
    for (int y = 0; y < field.height(); ++y) {
        for (int x = 0; x < field.width(); ++x) {
            field.at(x, y) = std::clamp(field.at(x, y) / total, 0, 255);
        }
    }
}

// True if (x, y) lies within core_clear_cells (plus `extra`) of the core area: kept clear of
// mountains, outcrops, lakes and the river.
bool near_core(const GenParams& p, int x, int y, int extra = 0) {
    const int m = p.core_clear_cells + extra;
    const Rect& a = p.core_area;
    return x >= a.x - m && x < a.x + a.w + m && y >= a.y - m && y < a.y + a.h + m;
}

// The lowest value v such that at least `target` of the counted cells have field >= v. Cells with
// a negative value aren't counted (and never pass). Values are 0..max_value.
int threshold_for(const Grid<int>& field, int max_value, long long target) {
    std::vector<long long> histogram(static_cast<std::size_t>(max_value) + 1, 0);
    for (const int v : field.cells()) {
        if (v >= 0) {
            ++histogram[static_cast<std::size_t>(std::min(v, max_value))];
        }
    }
    int t = max_value + 1;
    long long count = 0;
    while (t > 0 && count < target) {
        --t;
        count += histogram[static_cast<std::size_t>(t)];
    }
    return t;
}

// Steps from the nearest source over cells where passable(cell) holds (gen::bfs).
template <class Passable>
Grid<int> distance_from(const std::vector<Cell>& sources, Passable passable) {
    Grid<int> dist(kMapW, kMapH, -1);
    gen::bfs(dist, sources, passable);
    return dist;
}

// Like distance_from, but stops `limit` steps out: cells farther away stay -1. Much cheaper when
// only the nearby distances matter.
Grid<int> distance_within(const std::vector<Cell>& sources, int limit) {
    Grid<int> dist(kMapW, kMapH, -1);
    std::vector<Cell> queue;
    queue.reserve(sources.size() * 2);
    for (const Cell s : sources) {
        if (dist.at(s) < 0) {
            dist.at(s) = 0;
            queue.push_back(s);
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Cell c = queue[head];
        const int next = dist.at(c) + 1;
        if (next > limit) {
            continue;
        }
        for (const Cell d : kDirs) {
            const Cell n{c.x + d.x, c.y + d.y};
            if (dist.in_bounds(n) && dist.at(n) < 0) {
                dist.at(n) = next;
                queue.push_back(n);
            }
        }
    }
    return dist;
}

// --- Mountains --------------------------------------------------------------------------------

// Mountain noise plus a bias towards the map edges, cut to mountain_share_pct of the map, rounded
// off by cellular automata; then small outcrops. Rock cells are marked 1 (their looks value comes
// later). Nothing near the core area.
void make_mountains(Map& map, Rng& rng, const GenParams& p, GenLayers& out) {
    noise255(out.mountain, rng, p.mountain_noise);
    const int reach = std::max(1, (kMapH / 2) * p.mountain_edge_reach_pct / 100);
    Grid<int> field(kMapW, kMapH, -1);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (near_core(p, x, y)) {
                continue;
            }
            const int d = std::min({x, y, kMapW - 1 - x, kMapH - 1 - y});
            const int bias = p.mountain_edge_bias * std::max(0, reach - d) / reach;
            field.at(x, y) = out.mountain.at(x, y) + bias;
        }
    }
    const int max_value = 255 + p.mountain_edge_bias;
    const int t = threshold_for(field, max_value,
                                static_cast<long long>(kCellCount) * p.mountain_share_pct / 100);
    Mask rock(kMapW, kMapH, 0);
    if (p.mountain_share_pct > 0) {
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                rock.at(x, y) = field.at(x, y) >= t ? 1 : 0;
            }
        }
    }
    // Cellular automata: rock with 5+ rock neighbours (of 8; off the map counts as rock) stays or
    // grows, with 3 or fewer it erodes; 4 keeps what it was. Worked on a copy with a 1-cell rock
    // border, so the inner loop needs no bounds checks.
    constexpr int kPw = kMapW + 2;
    std::vector<std::uint8_t> pad(static_cast<std::size_t>(kPw) * (kMapH + 2), 1);
    const auto padded = [&](int x, int y) -> std::uint8_t& {
        return pad[static_cast<std::size_t>(y + 1) * kPw + static_cast<std::size_t>(x + 1)];
    };
    for (int pass = 0; pass < p.mountain_smooth_passes; ++pass) {
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                padded(x, y) = rock.at(x, y);
            }
        }
        for (int y = 0; y < kMapH; ++y) {
            const std::uint8_t* up = &padded(-1, y - 1);
            const std::uint8_t* mid = &padded(-1, y);
            const std::uint8_t* down = &padded(-1, y + 1);
            for (int x = 0; x < kMapW; ++x) {
                const int n = up[x] + up[x + 1] + up[x + 2] + mid[x] + mid[x + 2] + down[x] +
                              down[x + 1] + down[x + 2];
                if (!near_core(p, x, y)) {
                    rock.at(x, y) = n >= 5 ? 1 : n <= 3 ? 0 : mid[x + 1];
                }
            }
        }
    }
    // Outcrops: the highest cells of a fine noise, on open ground away from the core.
    Grid<int> outcrop(kMapW, kMapH, 0);
    noise255(outcrop, rng, p.outcrop_noise);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (rock.at(x, y) != 0 || near_core(p, x, y)) {
                outcrop.at(x, y) = -1;
            }
        }
    }
    if (p.outcrop_share_pct > 0) {
        const int ot = threshold_for(
            outcrop, 255, static_cast<long long>(kCellCount) * p.outcrop_share_pct / 100);
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                if (outcrop.at(x, y) >= ot) {
                    rock.at(x, y) = 1;
                }
            }
        }
    }
    // No specks: pieces of rock (4-connected) smaller than rock_min_cells are cleared.
    Grid<int> seen(kMapW, kMapH, 0);
    std::vector<Cell> piece;
    for (int y0 = 0; y0 < kMapH; ++y0) {
        for (int x0 = 0; x0 < kMapW; ++x0) {
            if (rock.at(x0, y0) == 0 || seen.at(x0, y0) != 0) {
                continue;
            }
            piece.assign(1, Cell{x0, y0});
            seen.at(x0, y0) = 1;
            for (std::size_t head = 0; head < piece.size(); ++head) {
                const Cell c = piece[head];
                for (const Cell d : kDirs) {
                    const Cell n{c.x + d.x, c.y + d.y};
                    if (rock.in_bounds(n) && rock.at(n) != 0 && seen.at(n) == 0) {
                        seen.at(n) = 1;
                        piece.push_back(n);
                    }
                }
            }
            if (static_cast<int>(piece.size()) < p.rock_min_cells) {
                for (const Cell c : piece) {
                    rock.at(c) = 0;
                }
            }
        }
    }
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            map.rock.at(x, y) = rock.at(x, y);
        }
    }
}

// The open cells (not rock), as BFS sources, in row order.
std::vector<Cell> open_cells(const Map& map) {
    std::vector<Cell> out;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (map.rock.at(x, y) == 0) {
                out.push_back({x, y});
            }
        }
    }
    return out;
}

// Caves: deep inside the mountains, a noise opens pockets. Pockets smaller than cave_min_cells are
// filled back; each other one is joined to the nearest open ground by a 2-cell-wide tunnel along
// the shortest way out.
void make_caves(Map& map, Rng& rng, const GenParams& p, GenLayers& out) {
    Grid<int> cave(kMapW, kMapH, 0);
    noise255(cave, rng, p.cave_noise);
    // Depth inside rock, up to cave_depth_cells (deeper cells stay -1, and count as deep).
    const Grid<int> depth = distance_within(open_cells(map), p.cave_depth_cells);
    // The deep cells, and among them the share with the highest cave noise.
    long long deep = 0;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const int d = depth.at(x, y);
            const bool is_deep = map.rock.at(x, y) != 0 && (d < 0 || d >= p.cave_depth_cells);
            deep += is_deep ? 1 : 0;
            if (!is_deep) {
                cave.at(x, y) = -1;
            }
        }
    }
    if (deep == 0 || p.cave_share_pct == 0) {
        return;
    }
    const int t = threshold_for(cave, 255, deep * p.cave_share_pct / 100);
    Mask pocket(kMapW, kMapH, 0); // 1 = opened by the cave noise
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (cave.at(x, y) >= t) {
                pocket.at(x, y) = 1;
                map.rock.at(x, y) = 0;
            }
        }
    }
    // Each pocket in row order of its first cell.
    Grid<int> label(kMapW, kMapH, -1);
    Grid<int> seen(kMapW, kMapH, -1); // tunnel search stamps (one per pocket)
    Grid<int> parent(kMapW, kMapH, -1);
    int pockets = 0;
    for (int y0 = 0; y0 < kMapH; ++y0) {
        for (int x0 = 0; x0 < kMapW; ++x0) {
            if (pocket.at(x0, y0) == 0 || label.at(x0, y0) >= 0) {
                continue;
            }
            const int id = pockets++;
            std::vector<Cell> cells{{x0, y0}};
            label.at(x0, y0) = id;
            for (std::size_t head = 0; head < cells.size(); ++head) {
                const Cell c = cells[head];
                for (const Cell d : kDirs) {
                    const Cell n{c.x + d.x, c.y + d.y};
                    if (label.in_bounds(n) && pocket.at(n) != 0 && label.at(n) < 0) {
                        label.at(n) = id;
                        cells.push_back(n);
                    }
                }
            }
            if (static_cast<int>(cells.size()) < p.cave_min_cells) {
                for (const Cell c : cells) {
                    map.rock.at(c) = 1; // too small: filled back in
                }
                continue;
            }
            // The tunnel: breadth-first from the pocket through rock to the first open cell that
            // isn't part of it.
            std::vector<Cell> queue = cells;
            for (const Cell c : cells) {
                seen.at(c) = id;
                parent.at(c) = -1;
            }
            Cell exit{-1, -1};
            for (std::size_t head = 0; head < queue.size() && exit.x < 0; ++head) {
                const Cell c = queue[head];
                for (const Cell d : kDirs) {
                    const Cell n{c.x + d.x, c.y + d.y};
                    if (!seen.in_bounds(n) || seen.at(n) == id) {
                        continue;
                    }
                    seen.at(n) = id;
                    parent.at(n) = static_cast<int>(index_of(c.x, c.y));
                    if (map.rock.at(n) == 0 && label.at(n) != id) {
                        exit = n;
                        break;
                    }
                    queue.push_back(n);
                }
            }
            if (exit.x < 0) {
                for (const Cell c : cells) {
                    map.rock.at(c) = 1; // nowhere to come out (can't happen on a real map)
                }
                continue;
            }
            // Walk back from the exit, carving each rock cell and one beside it (2 wide).
            int at = parent.at(exit);
            int prev = static_cast<int>(index_of(exit.x, exit.y));
            while (at >= 0) {
                const int cx = at % kMapW;
                const int cy = at / kMapW;
                if (label.at(cx, cy) == id) {
                    break;
                }
                map.rock.at(cx, cy) = 0;
                const bool vertical = (prev % kMapW) == cx;
                const int wx = vertical ? cx + 1 : cx;
                const int wy = vertical ? cy : cy + 1;
                if (map.rock.in_bounds(wx, wy)) {
                    map.rock.at(wx, wy) = 0;
                }
                prev = at;
                at = parent.at(cx, cy);
            }
            ++out.caves;
        }
    }
}

// Rock's looks value: higher the deeper inside, with a little of the mountain noise.
void shade_rock(Map& map, const GenLayers& layers) {
    const Grid<int> depth = distance_within(open_cells(map), 8);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (map.rock.at(x, y) != 0) {
                const int d = depth.at(x, y) < 0 ? 8 : depth.at(x, y);
                const int v = 60 + std::min(d, 8) * 20 + (layers.mountain.at(x, y) & 31) - 16;
                map.rock.at(x, y) = static_cast<std::uint8_t>(std::clamp(v, 1, 255));
            }
        }
    }
}

// --- Water ------------------------------------------------------------------------------------

// Sets the disc of radius `r` around c to `g` (Deep never turns back to Shallow unless
// `overwrite`), clearing rock.
void stamp(Map& map, Cell c, int r, Ground g, bool overwrite) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const int x = c.x + dx;
            const int y = c.y + dy;
            if (!map.ground.in_bounds(x, y) || dx * dx + dy * dy > r * r) {
                continue;
            }
            Ground& cell = map.ground.at(x, y);
            if (overwrite || cell != Ground::Deep) {
                cell = g;
            }
            map.rock.at(x, y) = 0;
        }
    }
}

// The river (river_pct of maps): the cheapest path from a point on one edge to one on the
// opposite edge, where each cell costs 1 + noise / 16, plus river_mountain_cost through rock, so
// it meanders and skirts the mountains; it never passes near the core. Deep in the middle, shallow
// banks, and fords where the deep water gives way to shallow.
void make_river(Map& map, Rng& rng, const GenParams& p, GenLayers& out) {
    Grid<int> noise(kMapW, kMapH, 0);
    noise255(noise, rng, p.river_noise);
    if (static_cast<int>(rng.next_below(100u)) >= p.river_pct) {
        return;
    }
    const bool across = rng.next_below(2u) == 0; // west to east, else north to south
    Cell from{};
    Cell to{};
    if (across) {
        from = {0, rand_range(rng, kMapH * 15 / 100, kMapH * 85 / 100)};
        to = {kMapW - 1, rand_range(rng, kMapH * 15 / 100, kMapH * 85 / 100)};
    } else {
        from = {rand_range(rng, kMapW * 15 / 100, kMapW * 85 / 100), 0};
        to = {rand_range(rng, kMapW * 15 / 100, kMapW * 85 / 100), kMapH - 1};
    }
    const int width = p.river_deep_width / 2 + p.river_bank_width;
    // The search runs on every second cell each way (a quarter of the cells; a river doesn't need
    // finer steering), at node (nx, ny) = cell (2 nx, 2 ny).
    constexpr int kRw = kMapW / 2;
    constexpr int kRh = kMapH / 2;
    const auto node_cost = [&](int nx, int ny) {
        const int x = 2 * nx;
        const int y = 2 * ny;
        return 1 + noise.at(x, y) / 16 + (map.rock.at(x, y) != 0 ? p.river_mountain_cost : 0);
    };
    // Dijkstra over 8 neighbours (a straight step costs 10x the node's cost, a diagonal 14x, so
    // the river runs at any angle instead of in steps), with a bucket queue: costs are small
    // integers, so the cheapest node is always in the next non-empty bucket. Nodes near the core
    // are out of bounds for it. Buckets are filled and emptied in a fixed order, so the path is the
    // same everywhere.
    constexpr std::array<Cell, 8> kDirs8 = {
        {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
    const int max_step = 14 * (1 + 255 / 16 + p.river_mountain_cost);
    std::vector<std::vector<int>> buckets(static_cast<std::size_t>(max_step) + 1);
    std::vector<int> dist(static_cast<std::size_t>(kRw) * kRh, -1);
    std::vector<int> parent(static_cast<std::size_t>(kRw) * kRh, -1);
    const int start = std::min(from.y / 2, kRh - 1) * kRw + std::min(from.x / 2, kRw - 1);
    const int goal = std::min(to.y / 2, kRh - 1) * kRw + std::min(to.x / 2, kRw - 1);
    dist[static_cast<std::size_t>(start)] = 0;
    buckets[0].push_back(start);
    std::size_t pending = 1;
    for (int d = 0; pending > 0; ++d) {
        std::vector<int>& bucket = buckets[static_cast<std::size_t>(d % (max_step + 1))];
        for (std::size_t b = 0; b < bucket.size(); ++b) {
            const int i = bucket[b];
            --pending;
            if (dist[static_cast<std::size_t>(i)] != d) {
                continue; // a stale entry: a cheaper way was found
            }
            if (i == goal) {
                pending = 0;
                break;
            }
            const int cx = i % kRw;
            const int cy = i / kRw;
            for (const Cell dir : kDirs8) {
                const int nx = cx + dir.x;
                const int ny = cy + dir.y;
                if (nx < 0 || ny < 0 || nx >= kRw || ny >= kRh ||
                    near_core(p, 2 * nx, 2 * ny, width)) {
                    continue;
                }
                const int j = ny * kRw + nx;
                const int nd = d + (dir.x != 0 && dir.y != 0 ? 14 : 10) * node_cost(nx, ny);
                int& dj = dist[static_cast<std::size_t>(j)];
                if (dj < 0 || nd < dj) {
                    dj = nd;
                    parent[static_cast<std::size_t>(j)] = i;
                    buckets[static_cast<std::size_t>(nd % (max_step + 1))].push_back(j);
                    ++pending;
                }
            }
        }
        bucket.clear();
    }
    if (dist[static_cast<std::size_t>(goal)] < 0) {
        return; // no way across (can't happen on a real map)
    }
    std::vector<int> nodes;
    for (int i = goal; i >= 0; i = parent[static_cast<std::size_t>(i)]) {
        nodes.push_back(i);
        if (i == start) {
            break;
        }
    }
    std::reverse(nodes.begin(), nodes.end());
    // Back to cells: each node, and the cell halfway to the next, so the line is unbroken.
    std::vector<Cell> path;
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        const Cell a{2 * (nodes[k] % kRw), 2 * (nodes[k] / kRw)};
        path.push_back(a);
        if (k + 1 < nodes.size()) {
            const Cell b{2 * (nodes[k + 1] % kRw), 2 * (nodes[k + 1] / kRw)};
            path.push_back({(a.x + b.x) / 2, (a.y + b.y) / 2});
        }
    }
    const int deep_r = p.river_deep_width / 2;
    for (const Cell c : path) {
        stamp(map, c, width, Ground::Shallow, false);
        if (p.river_deep_width > 0) {
            stamp(map, c, deep_r, Ground::Deep, true);
        }
    }
    // Fords, spread along the middle 80% with some jitter.
    const int count = rand_range(rng, p.fords_min, p.fords_max);
    const int n = static_cast<int>(path.size());
    const int lo = n / 10;
    const int span = std::max(1, n - 2 * lo);
    for (int k = 0; k < count; ++k) {
        const int slot = span / count;
        const int jitter = slot / 4;
        const int centre =
            std::clamp(lo + k * slot + slot / 2 + rand_range(rng, -jitter, jitter), 0, n - 1);
        for (int j = centre - p.ford_length / 2; j <= centre + p.ford_length / 2; ++j) {
            if (j >= 0 && j < n) {
                stamp(map, path[static_cast<std::size_t>(j)], width, Ground::Shallow, true);
            }
        }
        out.fords.push_back(path[static_cast<std::size_t>(centre)]);
    }
    out.river = std::move(path);
}

// Lakes: noise-edged blobs with deep middles and shallow rims, away from the core and not on rock.
void make_lakes(Map& map, Rng& rng, const GenParams& p) {
    Grid<int> rough(kMapW, kMapH, 0);
    noise255(rough, rng, p.lake_rough);
    const int count = rand_range(rng, p.lakes_min, p.lakes_max);
    for (int k = 0; k < count; ++k) {
        for (int tries = 0; tries < 16; ++tries) {
            const int r = rand_range(rng, p.lake_radius_min, p.lake_radius_max);
            const int cx = rand_range(rng, 0, kMapW - 1);
            const int cy = rand_range(rng, 0, kMapH - 1);
            if (near_core(p, cx, cy, 2 * r) || map.rock.at(cx, cy) != 0 ||
                is_water(map.ground.at(cx, cy))) {
                continue;
            }
            const int inner = std::max(0, r - p.lake_rim_cells);
            for (int y = std::max(0, cy - 2 * r); y <= std::min(kMapH - 1, cy + 2 * r); ++y) {
                for (int x = std::max(0, cx - 2 * r); x <= std::min(kMapW - 1, cx + 2 * r); ++x) {
                    if (map.rock.at(x, y) != 0) {
                        continue;
                    }
                    // The roughness scales r^2 between 60% and 140%.
                    const int scale = 60 + rough.at(x, y) * 80 / 255;
                    const int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                    if (d2 * 100 > r * r * scale) {
                        continue;
                    }
                    Ground& g = map.ground.at(x, y);
                    if (d2 * 100 <= inner * inner * scale) {
                        g = Ground::Deep;
                    } else if (g != Ground::Deep) {
                        g = Ground::Shallow;
                    }
                }
            }
            break;
        }
    }
}

// --- Ground and plants ------------------------------------------------------------------------

// Wet ground (marsh or mud) near water where it's moist enough, gravel beside rock, and otherwise
// sand, soil or rich soil by fertility.
void make_ground(Map& map, const GenParams& p, const GenLayers& L) {
    std::vector<Cell> water;
    std::vector<Cell> rock;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (is_water(map.ground.at(x, y))) {
                water.push_back({x, y});
            }
            if (map.rock.at(x, y) != 0) {
                rock.push_back({x, y});
            }
        }
    }
    const Grid<int> to_water = distance_within(water, p.wet_reach_cells);
    const Grid<int> to_rock = distance_within(rock, p.gravel_reach_cells);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            Ground& g = map.ground.at(x, y);
            if (is_water(g)) {
                continue;
            }
            const int f = L.fertility.at(x, y) * 100 / 255;
            const int m = L.moisture.at(x, y) * 100 / 255;
            const int dw = to_water.at(x, y);
            const int dr = to_rock.at(x, y);
            if (dw >= 0 && dw <= p.wet_reach_cells && m >= p.wet_moisture_pct) {
                g = m >= p.marsh_moisture_pct ? Ground::Marsh : Ground::Mud;
            } else if (dr >= 0 && dr <= p.gravel_reach_cells) {
                g = Ground::Gravel;
            } else if (f < p.sand_fertility_pct) {
                g = Ground::Sand;
            } else if (f >= p.rich_fertility_pct) {
                g = Ground::RichSoil;
            } else {
                g = Ground::Soil;
            }
        }
    }
}

// Trees clumped into forests: none where the forest noise is under forest_threshold_pct, rising to
// tree_rich_pct (rich soil) or tree_soil_pct (soil) forest_ramp_pct above it. Grass on fertile
// ground and a few bushes, for looks. One draw per open land cell, whatever grows.
void make_plants(Map& map, Rng& rng, const GenParams& p, const GenLayers& L) {
    Grid<int> forest(kMapW, kMapH, 0);
    noise255(forest, rng, p.forest_noise);
    const int thr = p.forest_threshold_pct;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const Ground g = map.ground.at(x, y);
            if (map.rock.at(x, y) != 0 || is_water(g)) {
                continue;
            }
            const std::uint32_t roll = rng.next_u32(); // two rolls of 0..99 from one draw
            const int tree_roll = static_cast<int>(((roll & 0xffffu) * 100u) >> 16u);
            const int bush_roll = static_cast<int>(((roll >> 16u) * 100u) >> 16u);
            const bool soil = g == Ground::Soil || g == Ground::RichSoil;
            if (!soil) {
                continue;
            }
            const int fo = forest.at(x, y) * 100 / 255;
            const int strength =
                fo >= thr ? std::min(100, (fo - thr) * 100 / p.forest_ramp_pct) : 0;
            const int base = g == Ground::RichSoil ? p.tree_rich_pct : p.tree_soil_pct;
            if (tree_roll < base * strength / 100) {
                map.plant.at(x, y) = Plant::Tree;
            } else if (L.fertility.at(x, y) * 100 / 255 >= p.grass_fertility_pct) {
                map.plant.at(x, y) = bush_roll < p.bush_pct ? Plant::Bush : Plant::Grass;
            }
        }
    }
}

// No sealed pockets: every walkable cell must be reachable from the core area. Unreached water-side
// cells (shallow or wet) become deep water, the rest rock.
void seal_pockets(Map& map, const GenParams& p) {
    std::vector<Cell> sources;
    const Rect& a = p.core_area;
    for (int y = a.y; y < a.y + a.h; ++y) {
        for (int x = a.x; x < a.x + a.w; ++x) {
            if (is_walkable(map, {x, y})) {
                sources.push_back({x, y});
            }
        }
    }
    const Grid<int> reach = distance_from(sources, [&](Cell c) { return is_walkable(map, c); });
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (reach.at(x, y) >= 0 || !is_walkable(map, {x, y})) {
                continue;
            }
            Ground& g = map.ground.at(x, y);
            if (g == Ground::Shallow || g == Ground::Mud || g == Ground::Marsh) {
                g = Ground::Deep;
            } else {
                map.rock.at(x, y) = 1;
            }
            map.plant.at(x, y) = Plant::None;
        }
    }
}

// Every core spot on the subgrid whose cells all lie in the core area and pass placement_ok, in
// row order. The same result as calling placement_ok for each, but each cell's buildable test runs
// once, and each block of cells is tested once (a 9-subcell side always covers 5 cells, so
// neighbouring subcell positions share blocks).
std::vector<SubCell> core_spots(const Map& map, const GenParams& p) {
    static_assert(cells_under(SubCell{0, 0}, kCoreSub).w == cells_under(SubCell{1, 1}, kCoreSub).w,
                  "the core must cover the same number of cells at every alignment");
    constexpr int kBlock = cells_under(SubCell{0, 0}, kCoreSub).w;
    const Rect& a = p.core_area;
    Mask bad(kMapW, kMapH, 0); // not buildable, inside the core area
    for (int y = a.y; y < a.y + a.h; ++y) {
        for (int x = a.x; x < a.x + a.w; ++x) {
            bad.at(x, y) = is_buildable(map, {x, y}) ? 0 : 1;
        }
    }
    Grid<std::int8_t> block_ok(kMapW, kMapH, -1); // by top-left cell; -1 = not tested yet
    const auto test_block = [&](int bx, int by) {
        for (int y = by; y < by + kBlock; ++y) {
            for (int x = bx; x < bx + kBlock; ++x) {
                if (bad.at(x, y) != 0) {
                    return false;
                }
            }
        }
        return true;
    };
    std::vector<SubCell> spots;
    const int sx_end = (a.x + a.w) * kSubPerCell;
    const int sy_end = (a.y + a.h) * kSubPerCell;
    for (int y = a.y * kSubPerCell; y + kCoreSub <= sy_end; ++y) {
        for (int x = a.x * kSubPerCell; x + kCoreSub <= sx_end; ++x) {
            const Rect cells = cells_under(SubCell{x, y}, kCoreSub);
            std::int8_t& ok = block_ok.at(cells.x, cells.y);
            if (ok < 0) {
                ok = test_block(cells.x, cells.y) ? 1 : 0;
            }
            if (ok != 0) {
                spots.push_back(SubCell{x, y});
            }
        }
    }
    return spots;
}

// A spawn's 2x2 block: on dry walkable land, reachable, and far enough away.
bool spawn_spot_ok(const Map& map, const Grid<int>& dist, Cell s, const GenParams& p) {
    for (int y = s.y; y < s.y + 2; ++y) {
        for (int x = s.x; x < s.x + 2; ++x) {
            if (!dist.in_bounds(x, y) || dist.at(x, y) < 0 || is_water(map.ground.at(x, y))) {
                return false;
            }
        }
    }
    return dist.at(s) >= p.min_spawn_dist;
}

// Places the spawns (one per entry in spawn_waves): each on an edge or inside (by the seed),
// far enough from the core and from each other. False if they can't all be placed.
bool place_spawns(Map& map, const Grid<int>& dist, Rng& rng, const GenParams& p) {
    std::vector<Cell> edge;
    std::vector<Cell> inside;
    for (int y = 0; y + 1 < kMapH; ++y) {
        for (int x = 0; x + 1 < kMapW; ++x) {
            const Cell s{x, y};
            if (spawn_spot_ok(map, dist, s, p)) {
                (on_map_edge(s) ? edge : inside).push_back(s);
            }
        }
    }
    shuffle(edge, rng);
    shuffle(inside, rng);
    map.spawns.clear();
    for (const int wave : p.spawn_waves) {
        const bool want_edge = static_cast<int>(rng.next_below(100u)) < p.spawn_edge_pct;
        bool placed = false;
        for (int pass = 0; pass < 2 && !placed; ++pass) {
            const std::vector<Cell>& pool = (want_edge != (pass == 1)) ? edge : inside;
            for (const Cell s : pool) {
                bool spaced = true;
                for (const Spawn& other : map.spawns) {
                    spaced = spaced && std::max(std::abs(s.x - other.cell.x),
                                                std::abs(s.y - other.cell.y)) >= p.spawn_spacing;
                }
                if (spaced) {
                    map.spawns.push_back({s, on_map_edge(s), wave});
                    placed = true;
                    break;
                }
            }
        }
        if (!placed) {
            map.spawns.clear();
            return false;
        }
    }
    return true;
}

} // namespace

Grid<int> walk_distance_to_core(const Map& map) {
    Grid<int> dist(kMapW, kMapH, -1);
    if (!map.core) {
        return dist;
    }
    const Rect core = cells_under(*map.core, kCoreSub); // the cells the core covers
    // Sources: the cells around the core that enemies reach it from.
    std::vector<Cell> queue;
    for (int y = core.y - 1; y <= core.y + core.h; ++y) {
        for (int x = core.x - 1; x <= core.x + core.w; ++x) {
            const Cell c{x, y};
            const bool beside = !core.contains(c) && ((x >= core.x && x < core.x + core.w) ||
                                                      (y >= core.y && y < core.y + core.h));
            if (beside && is_walkable(map, c)) {
                dist.at(c) = 0;
                queue.push_back(c);
            }
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Cell c = queue[head];
        for (const Cell d : kDirs) {
            const Cell n{c.x + d.x, c.y + d.y};
            if (dist.in_bounds(n) && dist.at(n) < 0 && !core.contains(n) && can_step(map, c, n)) {
                dist.at(n) = dist.at(c) + 1;
                queue.push_back(n);
            }
        }
    }
    return dist;
}

Map make_fallback_map(std::uint64_t seed, const GenParams& p) {
    Map map;
    map.seed = seed;
    map.core_area = p.core_area;
    map.core = SubCell{p.core_area.x * kSubPerCell + (p.core_area.w * kSubPerCell - kCoreSub) / 2,
                       p.core_area.y * kSubPerCell + (p.core_area.h * kSubPerCell - kCoreSub) / 2};
    const Cell corners[3] = {{0, 0}, {kMapW - 2, kMapH - 2}, {0, kMapH - 2}};
    for (std::size_t i = 0; i < p.spawn_waves.size() && i < 3; ++i) {
        map.spawns.push_back({corners[i], true, p.spawn_waves[i]});
    }
    return map;
}

Map generate_map(std::uint64_t seed, const GenParams& params, GenLayers* layers) {
    GenParams p = params; // clamped to safe ranges: a tuning panel or a preset can't break it
    validate(p);
    for (int attempt = 0; attempt < p.attempts; ++attempt) {
        Rng rng(splitmix64(seed), kStreamTerrain + static_cast<std::uint64_t>(attempt));
        Map map;
        map.seed = seed;
        map.core_area = p.core_area;
        GenLayers L;
        noise255(L.fertility, rng, p.fertility_noise);
        noise255(L.moisture, rng, p.moisture_noise);
        make_mountains(map, rng, p, L);
        make_caves(map, rng, p, L);
        make_river(map, rng, p, L);
        make_lakes(map, rng, p);
        make_ground(map, p, L);
        make_plants(map, rng, p, L);
        seal_pockets(map, p);
        shade_rock(map, L);
        std::vector<SubCell> spots = core_spots(map, p);
        shuffle(spots, rng);
        const std::size_t tries = std::min(spots.size(), static_cast<std::size_t>(p.core_tries));
        for (std::size_t t = 0; t < tries; ++t) {
            map.core = spots[t];
            if (place_spawns(map, walk_distance_to_core(map), rng, p)) {
                map.stats.attempts = attempt + 1;
                if (layers != nullptr) {
                    L.valid = true;
                    *layers = std::move(L);
                }
                return map;
            }
        }
    }
    Map fallback = make_fallback_map(seed, p);
    fallback.stats.attempts = p.attempts;
    fallback.stats.fallback = true;
    if (layers != nullptr) {
        *layers = GenLayers{};
    }
    return fallback;
}

} // namespace pixeltd
