#include "core/mapgen.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <doctest/doctest.h>

#include "core/hash.hpp"
#include "core/terrain.hpp"

using namespace pixeltd;

namespace {

std::uint64_t map_hash(const Map& m) {
    Fnv1a h;
    for (const Ground v : m.ground.cells()) {
        h.add_byte(static_cast<std::uint8_t>(v));
    }
    for (const Plant v : m.plant.cells()) {
        h.add_byte(static_cast<std::uint8_t>(v));
    }
    for (const std::uint8_t v : m.rock.cells()) {
        h.add_byte(v);
    }
    h.add_int(m.core ? m.core->x : -1);
    h.add_int(m.core ? m.core->y : -1);
    for (const Spawn& s : m.spawns) {
        h.add_int(s.cell.x);
        h.add_int(s.cell.y);
        h.add_int(s.on_edge ? 1 : 0);
        h.add_int(s.opens_at_wave);
    }
    return h.value();
}

constexpr int kCells = kMapW * kMapH;

// Every rule a generated map must keep (design/world.md, How a map is made).
void check_map(const Map& m, const GenParams& p) {
    REQUIRE(m.core.has_value());
    const SubCell core = *m.core;
    REQUIRE(p.core_area.contains(cells_under(core, kCoreSub)));
    REQUIRE(placement_ok(m, core, kCoreSub));

    const Grid<int> dist = walk_distance_to_core(m);
    REQUIRE(m.spawns.size() == p.spawn_waves.size());
    for (std::size_t i = 0; i < m.spawns.size(); ++i) {
        const Spawn& s = m.spawns[i];
        CAPTURE(i);
        REQUIRE(s.opens_at_wave == p.spawn_waves[i]);
        REQUIRE(s.on_edge == on_map_edge(s.cell));
        for (int y = s.cell.y; y < s.cell.y + 2; ++y) {
            for (int x = s.cell.x; x < s.cell.x + 2; ++x) {
                REQUIRE(dist.in_bounds(x, y));
                REQUIRE_FALSE(is_rock(m, {x, y}));
                REQUIRE(dist.at(x, y) >= 0);
            }
        }
        REQUIRE(dist.at(s.cell) >= p.min_spawn_dist);
        for (std::size_t j = 0; j < i; ++j) {
            const Cell o = m.spawns[j].cell;
            REQUIRE(std::max(std::abs(s.cell.x - o.x), std::abs(s.cell.y - o.y)) >=
                    p.spawn_spacing);
        }
    }

    // No sealed pockets: every walkable cell is reachable from the core.
    int sealed = 0;
    const Rect core_cells = cells_under(*m.core, kCoreSub);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            sealed +=
                is_walkable(m, {x, y}) && dist.at(x, y) < 0 && !core_cells.contains(Cell{x, y}) ? 1
                                                                                                : 0;
        }
    }
    REQUIRE(sealed == 0);
}

} // namespace

TEST_CASE("the map is deterministic") {
    const Map a = generate_map(42);
    const Map b = generate_map(42);
    CHECK(a.ground == b.ground);
    CHECK(a.rock == b.rock);
    CHECK(a.core == b.core);
    CHECK(a.spawns == b.spawns);
    CHECK(map_hash(a) == map_hash(b));
}

TEST_CASE("different seeds give different maps") {
    std::vector<std::uint64_t> hashes;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        hashes.push_back(map_hash(generate_map(seed)));
    }
    std::sort(hashes.begin(), hashes.end());
    CHECK(std::adjacent_find(hashes.begin(), hashes.end()) == hashes.end());
}

// Pinned results: these must match on every compiler and platform (MSVC locally, GCC on CI). If
// the generator is changed on purpose, update the values in the same pull request and say why.
// Last re-pinned in #32: mountains with caves, a river and lakes, ground types and plants.
TEST_CASE("golden maps") {
    CHECK(map_hash(generate_map(1)) == 0x926dc8ea1e09959bull);
    CHECK(map_hash(generate_map(0xC0FFEEull)) == 0x922cac881cabaa73ull);
    CHECK(map_hash(generate_map(0xFFFFFFFFFFFFFFFFull)) == 0xf6e3c4814441fbc5ull);
}

TEST_CASE("water, wet ground and trees: what can be walked and built on") {
    Map m = make_fallback_map(1);
    const Cell c{40, 40};
    const auto set = [&](Ground g, Plant pl) {
        m.ground.at(c) = g;
        m.plant.at(c) = pl;
    };
    set(Ground::Deep, Plant::None);
    CHECK_FALSE(is_walkable(m, c));
    CHECK_FALSE(is_buildable(m, c));
    CHECK_FALSE(can_step(m, {39, 40}, c));
    set(Ground::Shallow, Plant::None);
    CHECK(is_walkable(m, c));
    CHECK_FALSE(is_buildable(m, c));
    for (const Ground g : {Ground::Mud, Ground::Marsh}) {
        set(g, Plant::None);
        CHECK(is_walkable(m, c));
        CHECK_FALSE(is_buildable(m, c));
    }
    for (const Ground g : {Ground::Soil, Ground::RichSoil, Ground::Sand, Ground::Gravel}) {
        set(g, Plant::Grass);
        CHECK(is_walkable(m, c));
        CHECK(is_buildable(m, c));
    }
    set(Ground::Soil, Plant::Tree);
    CHECK(is_walkable(m, c));
    CHECK_FALSE(is_buildable(m, c));
    CHECK_FALSE(footprint_ok(m, {39, 39}, 2));
    set(Ground::Soil, Plant::Bush);
    CHECK(footprint_ok(m, {39, 39}, 2));
}

TEST_CASE("rock can't be walked through or built on; open ground can") {
    Map m = make_fallback_map(1);
    m.rock.at(30, 30) = 1;
    CHECK(is_rock(m, {30, 30}));
    CHECK_FALSE(can_step(m, {29, 30}, {30, 30}));
    CHECK_FALSE(can_step(m, {30, 30}, {31, 30}));
    CHECK_FALSE(footprint_ok(m, {29, 29}, 2));
    CHECK(footprint_ok(m, {27, 29}, 2)); // right up to it
    CHECK(can_step(m, {20, 20}, {21, 20}));
    CHECK_FALSE(can_step(m, {0, 0}, {-1, 0})); // off the map
    CHECK(footprint_ok(m, {50, 50}, 4));
    CHECK_FALSE(footprint_ok(m, {kMapW - 1, 10}, 2)); // hangs off the map
}

TEST_CASE("footprints on the 8 px subgrid cover the cells under them") {
    // A light tower (3 subcells) always covers 2 cells a side, at even or odd positions.
    CHECK(cells_under(SubCell{10, 20}, kLightSub) == Rect{5, 10, 2, 2});
    CHECK(cells_under(SubCell{11, 21}, kLightSub) == Rect{5, 10, 2, 2});
    // A heavy tower (6) covers 3 cells at an even position and 4 at an odd one.
    CHECK(cells_under(SubCell{10, 10}, kHeavySub) == Rect{5, 5, 3, 3});
    CHECK(cells_under(SubCell{11, 11}, kHeavySub) == Rect{5, 5, 4, 4});
    // The core (9) always covers 5.
    CHECK(cells_under(SubCell{10, 10}, kCoreSub) == Rect{5, 5, 5, 5});
    CHECK(cells_under(SubCell{11, 10}, kCoreSub) == Rect{5, 5, 5, 5});
}

TEST_CASE("a placement is refused if any cell under it is bad") {
    Map m = make_fallback_map(1);
    CHECK(placement_ok(m, SubCell{21, 21}, kLightSub)); // covers cells 10..11
    m.rock.at(12, 12) = 1;                              // just outside it
    CHECK(placement_ok(m, SubCell{21, 21}, kLightSub));
    CHECK_FALSE(placement_ok(m, SubCell{22, 22}, kLightSub)); // now covers cell 12, 12
    m.rock.at(30, 30) = 1;
    CHECK_FALSE(placement_ok(m, SubCell{58, 58}, kHeavySub)); // cells 29..31
    CHECK(placement_ok(m, SubCell{50, 50}, kHeavySub));
    CHECK_FALSE(placement_ok(m, SubCell{-1, 5}, kLightSub));        // off the subgrid
    CHECK_FALSE(placement_ok(m, SubCell{kSubW - 2, 5}, kLightSub)); // hangs off the map
    CHECK(placement_ok(m, SubCell{kSubW - 3, 5}, kLightSub));
}

TEST_CASE("walking distance goes around rock formations") {
    Map m = make_fallback_map(1);
    const Rect core = cells_under(*m.core, kCoreSub); // the cells the core covers
    // A wall of rock (a ring) all round the core, 3 cells out: nothing outside can reach it.
    for (int y = core.y - 4; y <= core.y + core.h + 3; ++y) {
        for (int x = core.x - 4; x <= core.x + core.w + 3; ++x) {
            const bool ring = y == core.y - 4 || y == core.y + core.h + 3 || x == core.x - 4 ||
                              x == core.x + core.w + 3;
            if (ring) {
                m.rock.at(x, y) = 1;
            }
        }
    }
    const Grid<int> dist = walk_distance_to_core(m);
    CHECK(dist.at(core.x - 1, core.y) == 0);
    CHECK(dist.at(core.x - 3, core.y) == 2);
    CHECK(dist.at(0, 0) < 0);
    CHECK(dist.at(core.x, core.y) < 0); // the core's own cells aren't walked
}

TEST_CASE("mountains: about their share, mostly near the edges, never by the core") {
    const GenParams p;
    long long rock = 0;
    long long edge_rock = 0;
    long long edge_cells = 0;
    long long middle_rock = 0;
    long long middle_cells = 0;
    constexpr int kSeeds = 6;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        const Map m = generate_map(seed, p);
        REQUIRE_FALSE(m.stats.fallback);
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                const bool r = m.rock.at(x, y) != 0;
                rock += r ? 1 : 0;
                const int d = std::min({x, y, kMapW - 1 - x, kMapH - 1 - y});
                if (d < 60) {
                    edge_rock += r ? 1 : 0;
                    ++edge_cells;
                } else if (d >= 150) {
                    middle_rock += r ? 1 : 0;
                    ++middle_cells;
                }
                const Rect& a = p.core_area;
                if (x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h) {
                    REQUIRE(m.rock.at(x, y) == 0);
                }
            }
        }
    }
    const long long share_pct = rock * 100 / (static_cast<long long>(kSeeds) * kCells);
    CAPTURE(share_pct);
    CHECK(share_pct >= p.mountain_share_pct - 5);
    CHECK(share_pct <= p.mountain_share_pct + 5);
    CHECK(edge_rock * middle_cells > 2 * middle_rock * edge_cells); // twice as rocky by the edges
}

TEST_CASE("rivers run edge to edge with fords; caves open into the mountains") {
    const GenParams p;
    int rivers = 0;
    int caves = 0;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        CAPTURE(seed);
        GenLayers L;
        const Map m = generate_map(seed, p, &L);
        REQUIRE(L.valid);
        caves += L.caves > 0 ? 1 : 0;
        if (L.river.empty()) {
            continue;
        }
        ++rivers;
        const Cell a = L.river.front();
        const Cell b = L.river.back();
        const bool across = a.x == 0 && b.x >= kMapW - 2;
        const bool down = a.y == 0 && b.y >= kMapH - 2;
        CHECK((across || down));
        CHECK(static_cast<int>(L.fords.size()) >= p.fords_min);
        CHECK(static_cast<int>(L.fords.size()) <= p.fords_max);
        for (const Cell f : L.fords) {
            CHECK(m.ground.at(f) == Ground::Shallow); // walkable across
        }
        // Away from the fords the middle is deep water.
        int deep = 0;
        for (const Cell c : L.river) {
            deep += m.ground.at(c) == Ground::Deep ? 1 : 0;
        }
        CHECK(deep * 2 > static_cast<int>(L.river.size()));
    }
    CHECK(rivers >= 4); // 80% of maps
    CHECK(caves >= 4);
}

TEST_CASE("the fallback map is valid") {
    const GenParams p;
    const Map m = make_fallback_map(99, p);
    REQUIRE(m.core.has_value());
    CHECK(placement_ok(m, *m.core, kCoreSub));
    const Grid<int> dist = walk_distance_to_core(m);
    REQUIRE(m.spawns.size() == p.spawn_waves.size());
    for (const Spawn& s : m.spawns) {
        CHECK(dist.at(s.cell) >= p.min_spawn_dist);
        CHECK(s.on_edge);
    }
}

TEST_CASE("sweep: 120 seeds give valid maps without the fallback") {
    const GenParams p;
    for (std::uint64_t seed = 0; seed < 120; ++seed) {
        CAPTURE(seed);
        const Map m = generate_map(seed, p);
        REQUIRE_FALSE(m.stats.fallback);
        check_map(m, p);
    }
}

// Manual tuning aid: pixeltd_tests -tc="*map stats*" --no-skip
TEST_CASE("map stats (manual)" * doctest::skip()) {
    const GenParams p;
    constexpr int kSeeds = 300;
    long long attempts = 0;
    int max_attempts = 0;
    int fallbacks = 0;
    long long formation = 0;
    long long buildable = 0;
    int edge_spawns = 0;
    long long spawn_dist = 0;
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed) {
        const Map m = generate_map(seed, p);
        attempts += m.stats.attempts;
        max_attempts = std::max(max_attempts, m.stats.attempts);
        fallbacks += m.stats.fallback ? 1 : 0;
        const Grid<int> dist = walk_distance_to_core(m);
        for (const Spawn& s : m.spawns) {
            edge_spawns += s.on_edge ? 1 : 0;
            spawn_dist += dist.at(s.cell);
        }
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                formation += m.rock.at(x, y) != 0 ? 1 : 0;
                buildable +=
                    x % 2 == 0 && y % 2 == 0 && placement_ok(m, SubCell{x * 2, y * 2}, kLightSub)
                        ? 1
                        : 0;
            }
        }
    }
    const double cells = static_cast<double>(kSeeds) * kCells;
    MESSAGE("attempts avg ", static_cast<double>(attempts) / kSeeds, " max ", max_attempts,
            "  fallbacks ", fallbacks);
    MESSAGE("formation % ", 100.0 * static_cast<double>(formation) / cells,
            "  light spots buildable % ", 400.0 * static_cast<double>(buildable) / cells);
    MESSAGE("edge spawns % ", 100.0 * edge_spawns / (3.0 * kSeeds), "  spawn walk avg ",
            static_cast<double>(spawn_dist) / (3.0 * kSeeds));
}

// Manual timing aid (#32): what each generator stage costs, by turning stages off one at a time.
// pixeltd_tests -tc="*stage timing*" --no-skip
TEST_CASE("stage timing (manual)" * doctest::skip()) {
    struct Case {
        const char* name;
        void (*tweak)(GenParams&);
    };
    const Case cases[] = {
        {"everything", [](GenParams&) {}},
        {"no caves", [](GenParams& p) { p.cave_share_pct = 0; }},
        {"no river", [](GenParams& p) { p.river_pct = 0; }},
        {"no lakes", [](GenParams& p) { p.lakes_min = p.lakes_max = 0; }},
        {"no mountains", [](GenParams& p) { p.mountain_share_pct = 0; }},
        {"1 octave everywhere",
         [](GenParams& p) {
             for (NoiseSpec* n :
                  {&p.fertility_noise, &p.moisture_noise, &p.mountain_noise, &p.outcrop_noise,
                   &p.cave_noise, &p.river_noise, &p.lake_rough, &p.forest_noise}) {
                 n->octaves = 1;
                 n->warp_cells = 0;
             }
         }},
    };
    for (const Case& c : cases) {
        GenParams p;
        c.tweak(p);
        double ms = 0.0;
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            const auto t0 = std::chrono::steady_clock::now();
            const Map m = generate_map(seed, p);
            ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                      .count();
            CHECK(m.core.has_value());
        }
        MESSAGE(c.name, ": ", ms / 3, " ms");
    }
}
