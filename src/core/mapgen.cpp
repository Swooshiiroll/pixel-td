#include "core/mapgen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/gen_util.hpp"
#include "core/hash.hpp"
#include "core/rng.hpp"
#include "core/terrain.hpp"

namespace pixeltd {
namespace {

using gen::add_value_noise;
using gen::kDirs;
using gen::rand_range;
using Mask = Grid<std::uint8_t>;

// Attempt k uses Rng stream kStreamTerrain + k, from splitmix64(seed).
constexpr std::uint64_t kStreamTerrain = 0x7e11a100ull;

constexpr int kCellCount = kMapW * kMapH;

// Marks about `target` cells with the highest field values (fields in [0, max_value]).
Mask top_cells(const Grid<int>& field, int max_value, int target) {
    std::vector<int> histogram(static_cast<std::size_t>(max_value) + 1, 0);
    for (const int v : field.cells()) {
        ++histogram[static_cast<std::size_t>(v)];
    }
    int threshold = max_value + 1;
    int count = 0;
    while (threshold > 0 && count < target) {
        --threshold;
        count += histogram[static_cast<std::size_t>(threshold)];
    }
    Mask out(kMapW, kMapH, 0);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            out.at(x, y) = field.at(x, y) >= threshold ? 1 : 0;
        }
    }
    return out;
}

template <class T> void shuffle(std::vector<T>& v, Rng& rng) {
    for (std::size_t i = v.size(); i > 1; --i) {
        const std::size_t j = rng.next_below(static_cast<std::uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

// Rolling height from three octaves of noise, stretched to 0..ground_max, with mesa regions
// terraced into flat steps (their edges become cliff faces).
void make_height(Map& map, Rng& rng, const GenParams& p) {
    Grid<int> field(kMapW, kMapH, 0);
    add_value_noise(field, rng, p.height_step, 4);
    add_value_noise(field, rng, std::max(2, p.height_step / 2), 2);
    add_value_noise(field, rng, std::max(2, p.height_step / 4), 1);
    const auto [lo_it, hi_it] = std::minmax_element(field.cells().begin(), field.cells().end());
    const int lo = *lo_it;
    const int span = std::max(1, *hi_it - lo);

    Grid<int> mesa_noise(kMapW, kMapH, 0);
    add_value_noise(mesa_noise, rng, p.mesa_noise_step, 1);
    const Mask mesa = top_cells(mesa_noise, 255, kCellCount * p.mesa_share_pct / 100);

    const int band = std::max(1, p.mesa_band);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            int h = (field.at(x, y) - lo) * p.ground_max / span;
            if (mesa.at(x, y) != 0) {
                h = std::min(p.ground_max, h / band * band + band / 2);
            }
            map.height.at(x, y) = static_cast<std::uint8_t>(h);
        }
    }
}

// Rock formations: blobs with noise-shaped edges, standing at formation_height_min..255.
void place_formations(Map& map, Rng& rng, const GenParams& p) {
    Grid<int> rough(kMapW, kMapH, 0);
    add_value_noise(rough, rng, 6, 1); // 0..255, shapes the edges and the tops
    const int count = rand_range(rng, p.formations_min, p.formations_max);
    for (int k = 0; k < count; ++k) {
        const int cx = rand_range(rng, 0, kMapW - 1);
        const int cy = rand_range(rng, 0, kMapH - 1);
        const int r = rand_range(rng, p.formation_radius_min, p.formation_radius_max);
        for (int y = std::max(0, cy - 2 * r); y <= std::min(kMapH - 1, cy + 2 * r); ++y) {
            for (int x = std::max(0, cx - 2 * r); x <= std::min(kMapW - 1, cx + 2 * r); ++x) {
                const int n = rough.at(x, y);
                const int scale = 45 + n * 110 / 255; // 45%..155% of r^2
                const int dx = x - cx;
                const int dy = y - cy;
                if ((dx * dx + dy * dy) * 100 <= r * r * scale) {
                    map.formation.at(x, y) = 1;
                    map.height.at(x, y) = static_cast<std::uint8_t>(
                        p.formation_height_min + n * (255 - p.formation_height_min) / 255);
                }
            }
        }
    }
}

void paint_materials(Map& map, Rng& rng, const GenParams& p) {
    Grid<int> field(kMapW, kMapH, 0);
    add_value_noise(field, rng, p.material_noise_step, 1);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            map.material.at(x, y) = static_cast<Material>(field.at(x, y) * kMaterials / 256);
        }
    }
}

bool shares_ok(const Map& map, const GenParams& p) {
    int formation = 0;
    int cliff = 0;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            formation += map.formation.at(x, y) != 0 ? 1 : 0;
            cliff += is_cliff_cell(map, {x, y}, p) ? 1 : 0;
        }
    }
    return formation * 100 <= p.formation_share_max_pct * kCellCount &&
           cliff * 100 >= p.cliff_share_min_pct * kCellCount &&
           cliff * 100 <= p.cliff_share_max_pct * kCellCount;
}

// A spawn's 2x2 block: no formation, reachable, walkable within itself, and far enough away.
bool spawn_spot_ok(const Map& map, const Grid<int>& dist, Cell s, const GenParams& p) {
    for (int y = s.y; y < s.y + 2; ++y) {
        for (int x = s.x; x < s.x + 2; ++x) {
            if (!dist.in_bounds(x, y) || dist.at(x, y) < 0 || is_formation(map, {x, y})) {
                return false;
            }
        }
    }
    return dist.at(s) >= p.min_spawn_dist && can_step(map, s, {s.x + 1, s.y}, p) &&
           can_step(map, s, {s.x, s.y + 1}, p) &&
           can_step(map, {s.x + 1, s.y + 1}, {s.x + 1, s.y}, p) &&
           can_step(map, {s.x + 1, s.y + 1}, {s.x, s.y + 1}, p);
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

Grid<int> walk_distance_to_core(const Map& map, const GenParams& p) {
    Grid<int> dist(kMapW, kMapH, -1);
    if (!map.core) {
        return dist;
    }
    const Rect core{map.core->x, map.core->y, kCoreSize, kCoreSize};
    // Sources: the cells around the core that enemies reach it from.
    std::vector<Cell> queue;
    for (int y = core.y - 1; y <= core.y + core.h; ++y) {
        for (int x = core.x - 1; x <= core.x + core.w; ++x) {
            const Cell c{x, y};
            const bool beside = !core.contains(c) && ((x >= core.x && x < core.x + core.w) ||
                                                      (y >= core.y && y < core.y + core.h));
            if (beside && dist.in_bounds(c) && !is_formation(map, c)) {
                dist.at(c) = 0;
                queue.push_back(c);
            }
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Cell c = queue[head];
        for (const Cell d : kDirs) {
            const Cell n{c.x + d.x, c.y + d.y};
            if (dist.in_bounds(n) && dist.at(n) < 0 && !core.contains(n) &&
                can_step(map, c, n, p)) {
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
    map.height.fill(100);
    map.core_area = p.core_area;
    map.core = Cell{p.core_area.x + (p.core_area.w - kCoreSize) / 2,
                    p.core_area.y + (p.core_area.h - kCoreSize) / 2};
    const Cell corners[3] = {{0, 0}, {kMapW - 2, kMapH - 2}, {0, kMapH - 2}};
    for (std::size_t i = 0; i < p.spawn_waves.size() && i < 3; ++i) {
        map.spawns.push_back({corners[i], true, p.spawn_waves[i]});
    }
    return map;
}

Map generate_map(std::uint64_t seed, const GenParams& p) {
    for (int attempt = 0; attempt < p.attempts; ++attempt) {
        Rng rng(splitmix64(seed), kStreamTerrain + static_cast<std::uint64_t>(attempt));
        Map map;
        map.seed = seed;
        map.core_area = p.core_area;
        make_height(map, rng, p);
        place_formations(map, rng, p);
        paint_materials(map, rng, p);
        if (!shares_ok(map, p)) {
            continue;
        }
        // The core: a seeded pick among the buildable 6x6 spots in the core area.
        std::vector<Cell> spots;
        const Rect& a = p.core_area;
        for (int y = a.y; y + kCoreSize <= a.y + a.h; ++y) {
            for (int x = a.x; x + kCoreSize <= a.x + a.w; ++x) {
                if (footprint_ok(map, {x, y}, kCoreSize, p)) {
                    spots.push_back({x, y});
                }
            }
        }
        shuffle(spots, rng);
        const std::size_t tries = std::min(spots.size(), static_cast<std::size_t>(p.core_tries));
        for (std::size_t t = 0; t < tries; ++t) {
            map.core = spots[t];
            if (place_spawns(map, walk_distance_to_core(map, p), rng, p)) {
                map.stats.attempts = attempt + 1;
                return map;
            }
        }
    }
    Map fallback = make_fallback_map(seed, p);
    fallback.stats.attempts = p.attempts;
    fallback.stats.fallback = true;
    return fallback;
}

} // namespace pixeltd
