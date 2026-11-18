#include "core/mapgen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/hash.hpp"
#include "core/rng.hpp"

namespace pixeltd {
namespace {

// Each step draws from its own Rng stream, so retuning one never shifts the others. Attempt k of
// stage 1 uses kStreamTerrain + k.
constexpr std::uint64_t kStreamTerrain = 0x51a9e100ull;

constexpr int kCellCount = kMapW * kMapH;

// Two octaves of value noise (weights 2 and 1, each in [0, 255]) give fields in [0, 765].
constexpr int kNoiseMax = 3 * 255;

using Mask = Grid<std::uint8_t>;

int rand_range(Rng& rng, int lo, int hi) { // inclusive
    return lo + static_cast<int>(rng.next_below(static_cast<std::uint32_t>(hi - lo + 1)));
}

constexpr std::array<Cell, 4> kDirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// Breadth-first steps over 4-connected cells where passable(cell) holds, from `sources`.
// Unreached cells stay -1. The queue order is fixed, so results are the same everywhere.
template <class Passable>
void bfs(Grid<int>& dist, const std::vector<Cell>& sources, Passable passable) {
    dist.fill(-1);
    std::vector<Cell> queue;
    queue.reserve(static_cast<std::size_t>(dist.width() * dist.height()));
    for (const Cell s : sources) {
        if (dist.at(s) < 0) {
            dist.at(s) = 0;
            queue.push_back(s);
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Cell c = queue[head];
        const int next = dist.at(c) + 1;
        for (const Cell d : kDirs) {
            const Cell n{c.x + d.x, c.y + d.y};
            if (dist.in_bounds(n) && dist.at(n) < 0 && passable(n)) {
                dist.at(n) = next;
                queue.push_back(n);
            }
        }
    }
}

// Adds integer value noise to `field`: random lattice values in [0, 255] every `step` cells,
// blended with a smoothstep curve, times `weight`. Integer-only, so every platform agrees.
void add_value_noise(Grid<int>& field, Rng& rng, int step, int weight) {
    const int lw = field.width() / step + 2;
    const int lh = field.height() / step + 2;
    std::vector<int> lattice(static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh));
    for (int& v : lattice) {
        v = static_cast<int>(rng.next_below(256u));
    }
    // Smoothstep weight for offset f within a step, in [0, 256]: 256 * (3t^2 - 2t^3), t = f/step.
    std::vector<int> ease(static_cast<std::size_t>(step));
    for (int f = 0; f < step; ++f) {
        const long long t2 = static_cast<long long>(f) * f;
        const long long num = 256LL * (3LL * t2 * step - 2LL * t2 * f);
        ease[static_cast<std::size_t>(f)] =
            static_cast<int>(num / (static_cast<long long>(step) * step * step));
    }
    const auto lat = [&](int gx, int gy) {
        return lattice[static_cast<std::size_t>(gy) * static_cast<std::size_t>(lw) +
                       static_cast<std::size_t>(gx)];
    };
    for (int y = 0; y < field.height(); ++y) {
        const int gy = y / step;
        const int wy = ease[static_cast<std::size_t>(y % step)];
        for (int x = 0; x < field.width(); ++x) {
            const int gx = x / step;
            const int wx = ease[static_cast<std::size_t>(x % step)];
            const int top = lat(gx, gy) * (256 - wx) + lat(gx + 1, gy) * wx;
            const int bottom = lat(gx, gy + 1) * (256 - wx) + lat(gx + 1, gy + 1) * wx;
            field.at(x, y) += weight * ((top * (256 - wy) + bottom * wy) >> 16);
        }
    }
}

// Marks about `target` cells with the highest field values, only where `allowed` (if given).
Mask top_cells(const Grid<int>& field, int target, const Mask* allowed) {
    std::vector<int> histogram(static_cast<std::size_t>(kNoiseMax) + 1, 0);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (allowed == nullptr || allowed->at(x, y) != 0) {
                ++histogram[static_cast<std::size_t>(field.at(x, y))];
            }
        }
    }
    int threshold = kNoiseMax + 1;
    int count = 0;
    while (threshold > 0 && count < target) {
        --threshold;
        count += histogram[static_cast<std::size_t>(threshold)];
    }
    Mask out(kMapW, kMapH, 0);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const bool ok = allowed == nullptr || allowed->at(x, y) != 0;
            out.at(x, y) = ok && field.at(x, y) >= threshold ? 1 : 0;
        }
    }
    return out;
}

// One majority pass: a cell is set when at least 5 of its 3x3 neighbourhood are. Off-map cells
// count as the cell itself, so the border doesn't erode shapes. Cells outside `allowed` stay 0.
void smooth_majority(Mask& mask, const Mask* allowed) {
    const Mask before = mask;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    n += before.in_bounds(x + dx, y + dy) ? before.at(x + dx, y + dy)
                                                          : before.at(x, y);
                }
            }
            const bool ok = allowed == nullptr || allowed->at(x, y) != 0;
            mask.at(x, y) = ok && n >= 5 ? 1 : 0;
        }
    }
}

// Flips every 4-connected region of cells equal to `value` that has fewer than `min_cells`.
void remove_small_regions(Mask& mask, std::uint8_t value, int min_cells) {
    Mask seen(kMapW, kMapH, 0);
    std::vector<Cell> region;
    region.reserve(kCellCount);
    const auto flipped = static_cast<std::uint8_t>(value != 0 ? 0 : 1);
    for (int sy = 0; sy < kMapH; ++sy) {
        for (int sx = 0; sx < kMapW; ++sx) {
            if (mask.at(sx, sy) != value || seen.at(sx, sy) != 0) {
                continue;
            }
            region.clear();
            region.push_back({sx, sy});
            seen.at(sx, sy) = 1;
            for (std::size_t head = 0; head < region.size(); ++head) {
                const Cell c = region[head];
                for (const Cell d : kDirs) {
                    const Cell n{c.x + d.x, c.y + d.y};
                    if (mask.in_bounds(n) && seen.at(n) == 0 && mask.at(n) == value) {
                        seen.at(n) = 1;
                        region.push_back(n);
                    }
                }
            }
            if (static_cast<int>(region.size()) < min_cells) {
                for (const Cell c : region) {
                    mask.at(c) = flipped;
                }
            }
        }
    }
}

// Rock piece shapes as 2x2 bit masks: bit 0 = (0,0), 1 = (1,0), 2 = (0,1), 3 = (1,1).
constexpr std::array<unsigned, 6> kRockShapes = {0b0001u, 0b0011u, 0b0101u,
                                                 0b1111u, 0b0111u, 0b1011u};

// Places one random rock piece with its top-left at (x, y), if every cell it needs is ground.
void place_rock_piece(Map& map, Rng& rng, int x, int y) {
    const unsigned shape =
        kRockShapes[rng.next_below(static_cast<std::uint32_t>(kRockShapes.size()))];
    for (int bit = 0; bit < 4; ++bit) {
        const int cx = x + (bit & 1);
        const int cy = y + (bit >> 1);
        if ((shape >> bit & 1u) != 0 &&
            (!map.terrain.in_bounds(cx, cy) || map.terrain.at(cx, cy) != Terrain::Ground)) {
            return;
        }
    }
    for (int bit = 0; bit < 4; ++bit) {
        if ((shape >> bit & 1u) != 0) {
            map.terrain.at(x + (bit & 1), y + (bit >> 1)) = Terrain::Rock;
        }
    }
}

void place_rocks(Map& map, Rng& rng, const GenParams& p) {
    const int fields = rand_range(rng, p.rock_fields_min, p.rock_fields_max);
    for (int f = 0; f < fields; ++f) {
        const int cx = rand_range(rng, 0, kMapW - 1);
        const int cy = rand_range(rng, 0, kMapH - 1);
        const int pieces = rand_range(rng, p.rock_field_pieces_min, p.rock_field_pieces_max);
        for (int i = 0; i < pieces; ++i) {
            const int r = p.rock_field_radius;
            place_rock_piece(map, rng, cx + rand_range(rng, -r, r), cy + rand_range(rng, -r, r));
        }
    }
    const int loose = rand_range(rng, p.rock_loose_min, p.rock_loose_max);
    for (int i = 0; i < loose; ++i) {
        place_rock_piece(map, rng, rand_range(rng, 0, kMapW - 2), rand_range(rng, 0, kMapH - 2));
    }
}

// Fills map.terrain. False if the shares came out of range (the attempt is then rejected).
bool generate_terrain(Map& map, Rng& rng, const GenParams& p) {
    // High ground: the top share of a two-octave noise field, smoothed and cleaned up.
    Grid<int> field(kMapW, kMapH, 0);
    add_value_noise(field, rng, p.high_noise_step, 2);
    add_value_noise(field, rng, std::max(2, p.high_noise_step / 2), 1);
    const int high_pct = rand_range(rng, p.high_share_min_pct, p.high_share_max_pct);
    Mask high = top_cells(field, kCellCount * high_pct / 100, nullptr);
    for (int i = 0; i < p.high_smooth_steps; ++i) {
        smooth_majority(high, nullptr);
    }
    remove_small_regions(high, 1, p.min_plateau_cells);
    remove_small_regions(high, 0, p.min_hole_cells);

    // Cliffs and mountains: the same method at a smaller scale, on low ground only.
    Mask low(kMapW, kMapH, 0);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            low.at(x, y) = high.at(x, y) != 0 ? 0 : 1;
        }
    }
    field.fill(0);
    add_value_noise(field, rng, p.cliff_noise_step, 2);
    add_value_noise(field, rng, std::max(2, p.cliff_noise_step / 2), 1);
    const int cliff_pct = rand_range(rng, p.cliff_share_min_pct, p.cliff_share_max_pct);
    Mask cliff = top_cells(field, kCellCount * cliff_pct / 100, &low);
    smooth_majority(cliff, &low);
    remove_small_regions(cliff, 1, p.min_cliff_cells);

    int high_cells = 0;
    int cliff_cells = 0;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            Terrain t = Terrain::Ground;
            if (high.at(x, y) != 0) {
                t = Terrain::High;
                ++high_cells;
            } else if (cliff.at(x, y) != 0) {
                t = Terrain::Cliff;
                ++cliff_cells;
            }
            map.terrain.at(x, y) = t;
        }
    }

    place_rocks(map, rng, p);

    return high_cells * 100 >= p.high_final_min_pct * kCellCount &&
           high_cells * 100 <= p.high_final_max_pct * kCellCount &&
           cliff_cells * 100 <= p.cliff_final_max_pct * kCellCount;
}

bool zone_ok(const Map& map, const Reachability& reach, const GenParams& p) {
    const std::vector<Cell> spots = legal_core_spots(map);
    if (static_cast<int>(spots.size()) < p.min_legal_spots) {
        return false;
    }
    for (const Cell s : spots) {
        if (!reach.routable(s)) {
            return false;
        }
    }
    return reach.farthest_edge_from(map.zone) >= p.min_spawn_dist;
}

} // namespace

// --- BrushFit ------------------------------------------------------------------------------------

BrushFit::BrushFit(const Grid<Terrain>& terrain) : square_(terrain.width(), terrain.height(), 0) {
    const int w = terrain.width();
    const int h = terrain.height();
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            if (terrain.at(x, y) != Terrain::Ground) {
                continue;
            }
            const int right = x + 1 < w ? square_.at(x + 1, y) : 0;
            const int down = y + 1 < h ? square_.at(x, y + 1) : 0;
            const int diag = x + 1 < w && y + 1 < h ? square_.at(x + 1, y + 1) : 0;
            const int side = 1 + std::min({right, down, diag});
            square_.at(x, y) = static_cast<std::uint8_t>(std::min(side, kMaxBrush));
        }
    }
}

int BrushFit::square_at(int x, int y) const noexcept {
    return square_.in_bounds(x, y) ? square_.at(x, y) : 0;
}

bool BrushFit::fits(Cell spine, int width) const noexcept {
    const int back = (width - 1) / 2;
    return square_at(spine.x - back, spine.y - back) >= width;
}

// --- Reachability -------------------------------------------------------------------------------

Reachability::Reachability(const Grid<Terrain>& terrain, const GenParams& params)
    : terrain_(&terrain), params_(params), fit_(terrain), edge_dist_(kMapW, kMapH, -1) {
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const bool on_edge = x == 0 || y == 0 || x == kMapW - 2 || y == kMapH - 2;
            if (on_edge && fit_.fits({x, y}, 2)) {
                edge_entries_.push_back({x, y});
            }
        }
    }
    bfs(edge_dist_, edge_entries_, [this](Cell c) { return fit_.fits(c, 2); });
}

void core_access_cells(Cell s, std::vector<Cell>& out) {
    out.clear();
    for (int y = s.y - 1; y <= s.y + kCoreSize - 1; ++y) {
        out.push_back({s.x - 2, y});
        out.push_back({s.x + kCoreSize, y});
    }
    for (int x = s.x - 1; x <= s.x + kCoreSize - 1; ++x) {
        out.push_back({x, s.y - 2});
        out.push_back({x, s.y + kCoreSize});
    }
}

bool Reachability::routable(Cell spot) const {
    std::vector<Cell> access;
    core_access_cells(spot, access);
    for (const Cell c : access) {
        if (fit_.fits(c, 2) && edge_dist_.at(c) >= 0) {
            return true;
        }
    }
    if (terrain_->at(spot) != Terrain::High) {
        return false;
    }

    // A high-ground core: try a ramp down its own plateau, within ramp_max cells of the core.
    const int r = params_.ramp_max;
    const Rect core{spot.x, spot.y, kCoreSize, kCoreSize};
    const Rect region{spot.x - r, spot.y - r, kCoreSize + 2 * r, kCoreSize + 2 * r};
    const Rect window{region.x - 2, region.y - 2, region.w + 4, region.h + 4};
    const auto local = [&](Cell c) { return Cell{c.x - window.x, c.y - window.y}; };

    // Plateau cells the ramp may use: high ground connected to the core, inside the region.
    Grid<std::uint8_t> plateau(window.w, window.h, 0);
    std::vector<Cell> queue{spot};
    plateau.at(local(spot)) = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Cell c = queue[head];
        for (const Cell d : kDirs) {
            const Cell n{c.x + d.x, c.y + d.y};
            if (region.contains(n) && terrain_->in_bounds(n) && plateau.at(local(n)) == 0 &&
                terrain_->at(n) == Terrain::High) {
                plateau.at(local(n)) = 1;
                queue.push_back(n);
            }
        }
    }
    const auto path_ok = [&](int x, int y) {
        const Cell c{x, y};
        if (!terrain_->in_bounds(c) || core.contains(c)) {
            return false;
        }
        const Terrain t = terrain_->at(c);
        return t == Terrain::Ground ||
               (t == Terrain::High && region.contains(c) && plateau.at(local(c)) != 0);
    };
    const auto ramp_walk = [&](Cell c) {
        return window.contains(c) && window.contains(Cell{c.x + 1, c.y + 1}) && path_ok(c.x, c.y) &&
               path_ok(c.x + 1, c.y) && path_ok(c.x, c.y + 1) && path_ok(c.x + 1, c.y + 1);
    };

    Grid<std::uint8_t> seen(window.w, window.h, 0);
    queue.clear();
    for (const Cell c : access) {
        if (ramp_walk(c) && seen.at(local(c)) == 0) {
            seen.at(local(c)) = 1;
            queue.push_back(c);
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Cell c = queue[head];
        if (fit_.fits(c, 2) && edge_dist_.at(c) >= 0) {
            return true;
        }
        for (const Cell d : kDirs) {
            const Cell n{c.x + d.x, c.y + d.y};
            if (ramp_walk(n) && seen.at(local(n)) == 0) {
                seen.at(local(n)) = 1;
                queue.push_back(n);
            }
        }
    }
    return false;
}

int Reachability::farthest_edge_from(const Rect& zone) const {
    const int pad = params_.ramp_max + 2;
    std::vector<Cell> sources;
    for (int y = zone.y - pad; y < zone.y + zone.h + pad; ++y) {
        for (int x = zone.x - pad; x < zone.x + zone.w + pad; ++x) {
            if (fit_.fits({x, y}, 2) && edge_dist_.at(x, y) >= 0) {
                sources.push_back({x, y});
            }
        }
    }
    if (sources.empty()) {
        return -1;
    }
    Grid<int> dist(kMapW, kMapH, -1);
    bfs(dist, sources, [this](Cell c) { return fit_.fits(c, 2); });
    int farthest = -1;
    for (const Cell e : edge_entries_) {
        farthest = std::max(farthest, dist.at(e));
    }
    return farthest;
}

// --- Stage 1 ------------------------------------------------------------------------------------

bool is_legal_core_spot(const Map& map, Cell spot) noexcept {
    if (!map.zone.contains(Rect{spot.x, spot.y, kCoreSize, kCoreSize})) {
        return false;
    }
    const Terrain first = map.terrain.at(spot);
    if (first != Terrain::Ground && first != Terrain::High) {
        return false;
    }
    for (int y = spot.y; y < spot.y + kCoreSize; ++y) {
        for (int x = spot.x; x < spot.x + kCoreSize; ++x) {
            if (map.terrain.at(x, y) != first) {
                return false;
            }
        }
    }
    return true;
}

std::vector<Cell> legal_core_spots(const Map& map) {
    std::vector<Cell> spots;
    for (int y = map.zone.y; y + kCoreSize <= map.zone.y + map.zone.h; ++y) {
        for (int x = map.zone.x; x + kCoreSize <= map.zone.x + map.zone.w; ++x) {
            if (is_legal_core_spot(map, {x, y})) {
                spots.push_back({x, y});
            }
        }
    }
    return spots;
}

Map make_fallback_map(std::uint64_t seed, const GenParams& params) {
    Map map;
    map.seed = seed;
    map.zone =
        Rect{kMapW - params.zone_w - 8, (kMapH - params.zone_h) / 2, params.zone_w, params.zone_h};
    return map;
}

Map generate_stage1(std::uint64_t seed, const GenParams& params) {
    for (int attempt = 0; attempt < params.stage1_attempts; ++attempt) {
        Rng rng(splitmix64(seed), kStreamTerrain + static_cast<std::uint64_t>(attempt));
        Map map;
        map.seed = seed;
        if (!generate_terrain(map, rng, params)) {
            continue;
        }
        const Reachability reach(map.terrain, params);
        for (int t = 0; t < params.zone_tries; ++t) {
            map.zone =
                Rect{rand_range(rng, 0, kMapW - params.zone_w),
                     rand_range(rng, 0, kMapH - params.zone_h), params.zone_w, params.zone_h};
            if (zone_ok(map, reach, params)) {
                map.stats.stage1_attempts = attempt + 1;
                map.stats.zone_tries = t + 1;
                return map;
            }
        }
    }
    Map fallback = make_fallback_map(seed, params);
    fallback.stats.stage1_attempts = params.stage1_attempts;
    fallback.stats.stage1_fallback = true;
    return fallback;
}

} // namespace pixeltd
