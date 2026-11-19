#include "core/mapgen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "core/erosion.hpp"
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

// Turns a raw fill_noise sum into the layer's 0..255 value (exact for one octave).
void normalize(Grid<int>& field, int total_weight) {
    const int total = std::max(1, total_weight);
    for (int y = 0; y < field.height(); ++y) {
        for (int x = 0; x < field.width(); ++x) {
            field.at(x, y) /= total;
        }
    }
}

// Rolling height from the height noise, stretched to 0..ground_max, with mesa regions terraced
// into steps (their edges become cliff faces). Terracing fades in over mesa_feather_cells from a
// mesa region's edge, and each table keeps mesa_relief of the slope under it.
void make_height(Map& map, Rng& rng, const GenParams& p, GenLayers& out) {
    Grid<int>& field = out.height_sum;
    fill_noise(field, rng, p.height, &out.octave, &out.octave_weight);
    const auto [lo_it, hi_it] = std::minmax_element(field.cells().begin(), field.cells().end());
    const int lo = *lo_it;
    const int span = std::max(1, *hi_it - lo);

    const int mesa_weight = fill_noise(out.mesa_noise, rng, p.mesa_noise);
    normalize(out.mesa_noise, mesa_weight);
    out.mesa = top_cells(out.mesa_noise, 255, kCellCount * p.mesa_share_pct / 100);
    const Mask& mesa = out.mesa;

    // The feather: each mesa cell's walking distance (4-neighbour steps) from the nearest cell
    // outside the mesa regions; terracing reaches full strength mesa_feather_cells in.
    Grid<int> dist(kMapW, kMapH, -1);
    std::vector<Cell> outside;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            if (mesa.at(x, y) == 0) {
                outside.push_back({x, y});
            }
        }
    }
    gen::bfs(dist, outside, [&](Cell c) { return mesa.at(c) != 0; });
    const int feather = p.mesa_feather_cells;

    const int band = std::max(1, p.mesa_band);
    const int relief = std::clamp(p.mesa_relief, 0, band);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const int smooth =
                static_cast<int>(static_cast<long long>(field.at(x, y) - lo) * p.ground_max / span);
            int h = smooth;
            int blend = 0;
            if (mesa.at(x, y) != 0) {
                // A whole-map mesa has no outside cells to measure from: fully terraced.
                const int d = dist.at(x, y) < 0 ? feather : dist.at(x, y);
                blend = feather <= 0 ? 256 : std::min(d, feather) * 256 / feather;
                // The table: its level, centred in the band, plus a compressed copy of the slope.
                const int level = smooth / band * band;
                const int terraced = std::min(p.ground_max, level + (band - relief) / 2 +
                                                                smooth % band * relief / band);
                h = smooth + (terraced - smooth) * blend / 256;
            }
            out.mesa_blend.at(x, y) = blend;
            map.height.at(x, y) = static_cast<std::uint8_t>(std::clamp(h, 0, p.ground_max));
        }
    }
}

// The natural terrains (#29): simplex octaves stretched to 0..ground_max, with no mesas. For the
// eroded terrain, hydraulic erosion then runs on the heights in fixed point, and the result is
// stretched again so it still spans 0..ground_max.
void make_natural_height(Map& map, Rng& rng, const GenParams& p, GenLayers& out) {
    Grid<int>& field = out.height_sum;
    fill_noise(field, rng, p.natural_height, &out.octave, &out.octave_weight);
    const auto stretch = [&](const Grid<int>& g, long long scale) {
        const auto [lo_it, hi_it] = std::minmax_element(g.cells().begin(), g.cells().end());
        const long long lo = *lo_it;
        const long long span = std::max(1LL, static_cast<long long>(*hi_it) - lo);
        Grid<int> s(kMapW, kMapH, 0);
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                s.at(x, y) = static_cast<int>((g.at(x, y) - lo) * p.ground_max * scale / span);
            }
        }
        return s;
    };
    Grid<int> q = stretch(field, kErosionOne); // heights in 1/65536 units
    if (p.terrain == TerrainAlgo::Eroded) {
        erode(q, rng, p);
        q = stretch(q, kErosionOne);
    }
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const int h = static_cast<int>((q.at(x, y) + kErosionOne / 2) / kErosionOne);
            map.height.at(x, y) = static_cast<std::uint8_t>(std::clamp(h, 0, p.ground_max));
        }
    }
}

// Rock formations: blobs with noise-shaped edges, standing at formation_height_min..255.
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
                    map.formation.at(x, y) = 1;
                    map.height.at(x, y) = static_cast<std::uint8_t>(
                        p.formation_height_min + n * (255 - p.formation_height_min) / 255);
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
    int cliff = 0;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            formation += map.formation.at(x, y) != 0 ? 1 : 0;
            cliff += is_cliff_cell(map, {x, y}, p) ? 1 : 0;
        }
    }
    const int cliff_min =
        p.terrain == TerrainAlgo::Value ? p.cliff_share_min_pct : p.natural_cliff_share_min_pct;
    return formation * 100 <= p.formation_share_max_pct * kCellCount &&
           cliff * 100 >= cliff_min * kCellCount &&
           cliff * 100 <= p.cliff_share_max_pct * kCellCount;
}

// Every core spot on the subgrid whose cells all lie in the core area and pass placement_ok, in
// row order. The same result as calling placement_ok for each, but each cell's formation and
// cliff test runs once, and each block of cells is tested once (a 9-subcell side always covers 5
// cells, so neighbouring subcell positions share blocks).
std::vector<SubCell> core_spots(const Map& map, const GenParams& p) {
    static_assert(cells_under(SubCell{0, 0}, kCoreSub).w == cells_under(SubCell{1, 1}, kCoreSub).w,
                  "the core must cover the same number of cells at every alignment");
    constexpr int kBlock = cells_under(SubCell{0, 0}, kCoreSub).w;
    const Rect& a = p.core_area;
    Mask bad(kMapW, kMapH, 0); // formation or cliff, inside the core area
    for (int y = a.y; y < a.y + a.h; ++y) {
        for (int x = a.x; x < a.x + a.w; ++x) {
            bad.at(x, y) = is_formation(map, {x, y}) || is_cliff_cell(map, {x, y}, p) ? 1 : 0;
        }
    }
    Grid<std::int8_t> block_ok(kMapW, kMapH, -1); // by top-left cell; -1 = not tested yet
    const auto test_block = [&](int bx, int by) {
        int lo = 255;
        int hi = 0;
        for (int y = by; y < by + kBlock; ++y) {
            for (int x = bx; x < bx + kBlock; ++x) {
                if (bad.at(x, y) != 0) {
                    return false;
                }
                lo = std::min(lo, static_cast<int>(map.height.at(x, y)));
                hi = std::max(hi, static_cast<int>(map.height.at(x, y)));
            }
        }
        return hi - lo <= p.flat_limit;
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
        if (p.terrain == TerrainAlgo::Value) {
            make_height(map, rng, p, noise);
        } else {
            make_natural_height(map, rng, p, noise);
        }
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
            if (place_spawns(map, walk_distance_to_core(map, p), rng, p)) {
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
