#include "core/mapgen.hpp"

#include <algorithm>
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

template <class T> void shuffle(std::vector<T>& v, Rng& rng) {
    for (std::size_t i = v.size(); i > 1; --i) {
        const std::size_t j = rng.next_below(static_cast<std::uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

// Turns a raw fill_noise sum into the layer's 0..255 value (exact for one octave).
void normalize(Grid<int>& field, int total_weight) {
    const int total = std::max(1, total_weight);
    for (int y = 0; y < field.height(); ++y) {
        for (int x = 0; x < field.width(); ++x) {
            field.at(x, y) /= total;
        }
    }
}

// Rock formations: blobs with noise-shaped edges. Each cell stores 1..255 from the roughness: how
// tall the rock stands there (looks only).
void place_formations(Map& map, Rng& rng, const GenParams& p, GenLayers& out) {
    Grid<int>& rough = out.rough;
    normalize(rough, fill_noise(rough, rng, p.formation_rough)); // 0..255
    const int count = rand_range(rng, p.formations_min, p.formations_max);
    for (int k = 0; k < count; ++k) {
        const int cx = rand_range(rng, 0, kMapW - 1);
        const int cy = rand_range(rng, 0, kMapH - 1);
        const int r = rand_range(rng, p.formation_radius_min, p.formation_radius_max);
        for (int y = std::max(0, cy - 2 * r); y <= std::min(kMapH - 1, cy + 2 * r); ++y) {
            for (int x = std::max(0, cx - 2 * r); x <= std::min(kMapW - 1, cx + 2 * r); ++x) {
                const int n = rough.at(x, y);
                // The roughness scales r^2 between the two percentages.
                const int scale = p.formation_scale_min_pct +
                                  n * (p.formation_scale_max_pct - p.formation_scale_min_pct) / 255;
                const int dx = x - cx;
                const int dy = y - cy;
                if ((dx * dx + dy * dy) * 100 <= r * r * scale) {
                    map.formation.at(x, y) = static_cast<std::uint8_t>(1 + n * 254 / 255);
                }
            }
        }
    }
}

void paint_materials(Map& map, Rng& rng, const GenParams& p, GenLayers& out) {
    Grid<int>& field = out.material;
    normalize(field, fill_noise(field, rng, p.material_noise)); // 0..255
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            map.material.at(x, y) =
                static_cast<Material>(std::clamp(field.at(x, y), 0, 255) * kMaterials / 256);
        }
    }
}

bool shares_ok(const Map& map, const GenParams& p) {
    int formation = 0;
    for (const std::uint8_t f : map.formation.cells()) {
        formation += f != 0 ? 1 : 0;
    }
    return formation * 100 <= p.formation_share_max_pct * kCellCount;
}

// Every core spot on the subgrid whose cells all lie in the core area and pass placement_ok, in
// row order. The same result as calling placement_ok for each, but each cell's formation test
// runs once, and each block of cells is tested once (a 9-subcell side always covers 5
// cells, so neighbouring subcell positions share blocks).
std::vector<SubCell> core_spots(const Map& map, const GenParams& p) {
    static_assert(cells_under(SubCell{0, 0}, kCoreSub).w == cells_under(SubCell{1, 1}, kCoreSub).w,
                  "the core must cover the same number of cells at every alignment");
    constexpr int kBlock = cells_under(SubCell{0, 0}, kCoreSub).w;
    const Rect& a = p.core_area;
    Mask bad(kMapW, kMapH, 0); // formation, inside the core area
    for (int y = a.y; y < a.y + a.h; ++y) {
        for (int x = a.x; x < a.x + a.w; ++x) {
            bad.at(x, y) = is_formation(map, {x, y}) ? 1 : 0;
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

// A spawn's 2x2 block: no formation, reachable, and far enough away.
bool spawn_spot_ok(const Map& map, const Grid<int>& dist, Cell s, const GenParams& p) {
    for (int y = s.y; y < s.y + 2; ++y) {
        for (int x = s.x; x < s.x + 2; ++x) {
            if (!dist.in_bounds(x, y) || dist.at(x, y) < 0 || is_formation(map, {x, y})) {
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
        GenLayers noise;
        place_formations(map, rng, p, noise);
        paint_materials(map, rng, p, noise);
        if (!shares_ok(map, p)) {
            continue;
        }
        std::vector<SubCell> spots = core_spots(map, p);
        shuffle(spots, rng);
        const std::size_t tries = std::min(spots.size(), static_cast<std::size_t>(p.core_tries));
        for (std::size_t t = 0; t < tries; ++t) {
            map.core = spots[t];
            if (place_spawns(map, walk_distance_to_core(map), rng, p)) {
                map.stats.attempts = attempt + 1;
                if (layers != nullptr) {
                    noise.valid = true;
                    *layers = std::move(noise);
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
