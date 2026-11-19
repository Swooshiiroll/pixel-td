#include "core/mapgen.hpp"

#include <algorithm>
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
    for (const std::uint8_t v : m.height.cells()) {
        h.add_byte(v);
    }
    for (const Material v : m.material.cells()) {
        h.add_byte(static_cast<std::uint8_t>(v));
    }
    for (const std::uint8_t v : m.formation.cells()) {
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
    REQUIRE(placement_ok(m, core, kCoreSub, p));

    const Grid<int> dist = walk_distance_to_core(m, p);
    REQUIRE(m.spawns.size() == p.spawn_waves.size());
    for (std::size_t i = 0; i < m.spawns.size(); ++i) {
        const Spawn& s = m.spawns[i];
        CAPTURE(i);
        REQUIRE(s.opens_at_wave == p.spawn_waves[i]);
        REQUIRE(s.on_edge == on_map_edge(s.cell));
        for (int y = s.cell.y; y < s.cell.y + 2; ++y) {
            for (int x = s.cell.x; x < s.cell.x + 2; ++x) {
                REQUIRE(dist.in_bounds(x, y));
                REQUIRE_FALSE(is_formation(m, {x, y}));
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

    int formation = 0;
    int cliff = 0;
    bool heights_ok = true;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const bool f = m.formation.at(x, y) != 0;
            formation += f ? 1 : 0;
            cliff += is_cliff_cell(m, {x, y}, p) ? 1 : 0;
            heights_ok = heights_ok && (f ? m.height.at(x, y) >= p.formation_height_min
                                          : m.height.at(x, y) <= p.ground_max);
        }
    }
    REQUIRE(heights_ok);
    REQUIRE(formation * 100 <= p.formation_share_max_pct * kCells);
    REQUIRE(cliff * 100 >= p.cliff_share_min_pct * kCells);
    REQUIRE(cliff * 100 <= p.cliff_share_max_pct * kCells);
}

} // namespace

TEST_CASE("the map is deterministic") {
    const Map a = generate_map(42);
    const Map b = generate_map(42);
    CHECK(a.height == b.height);
    CHECK(a.material == b.material);
    CHECK(a.formation == b.formation);
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
// Last re-pinned in #19: mesa terraces every 20 height units instead of 40.
TEST_CASE("golden maps") {
    CHECK(map_hash(generate_map(1)) == 0x8b838b4e02876b73ull);
    CHECK(map_hash(generate_map(0xC0FFEEull)) == 0x5f8ab6e800dba6daull);
    CHECK(map_hash(generate_map(0xFFFFFFFFFFFFFFFFull)) == 0x1d7467a75b357970ull);
}

TEST_CASE("cliff steps block walking both ways and building beside them") {
    const GenParams p;
    Map m = make_fallback_map(1, p); // flat at height 100
    m.height.at(10, 10) = static_cast<std::uint8_t>(100 + p.cliff_step + 1);
    CHECK_FALSE(can_step(m, {9, 10}, {10, 10}, p));
    CHECK_FALSE(can_step(m, {10, 10}, {9, 10}, p));
    CHECK(is_cliff_cell(m, {9, 10}, p));
    CHECK(is_cliff_cell(m, {10, 10}, p));
    CHECK_FALSE(footprint_ok(m, {8, 9}, 2, p));
    m.height.at(10, 10) = static_cast<std::uint8_t>(100 + p.cliff_step);
    CHECK(can_step(m, {9, 10}, {10, 10}, p));
    CHECK_FALSE(is_cliff_cell(m, {9, 10}, p));
    CHECK(can_step(m, {20, 20}, {21, 20}, p));
    CHECK_FALSE(can_step(m, {0, 0}, {-1, 0}, p)); // off the map
}

TEST_CASE("rock formations can't be walked through or built on") {
    const GenParams p;
    Map m = make_fallback_map(1, p);
    m.formation.at(30, 30) = 1;
    m.height.at(30, 30) = 240;
    CHECK(is_formation(m, {30, 30}));
    CHECK_FALSE(can_step(m, {29, 30}, {30, 30}, p));
    CHECK_FALSE(can_step(m, {30, 30}, {31, 30}, p));
    CHECK_FALSE(footprint_ok(m, {29, 29}, 2, p));
    // A formation's sheer side isn't a cliff for the ground beside it: you can build right up to
    // it.
    CHECK_FALSE(is_cliff_cell(m, {29, 30}, p));
    CHECK(footprint_ok(m, {27, 29}, 2, p));
}

TEST_CASE("building needs fairly flat ground") {
    const GenParams p;
    Map m = make_fallback_map(1, p);
    m.height.at(41, 41) = static_cast<std::uint8_t>(100 + p.flat_limit);
    CHECK(footprint_ok(m, {40, 40}, 2, p));
    m.height.at(41, 41) = static_cast<std::uint8_t>(100 + p.flat_limit + 1);
    CHECK_FALSE(footprint_ok(m, {40, 40}, 2, p));
    CHECK(footprint_ok(m, {50, 50}, 4, p));
    CHECK_FALSE(footprint_ok(m, {kMapW - 1, 10}, 2, p)); // hangs off the map
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
    const GenParams p;
    Map m = make_fallback_map(1, p);                       // flat at height 100
    CHECK(placement_ok(m, SubCell{21, 21}, kLightSub, p)); // covers cells 10..11
    m.formation.at(12, 12) = 1;                            // just outside it
    m.height.at(12, 12) = 240;
    CHECK(placement_ok(m, SubCell{21, 21}, kLightSub, p));
    CHECK_FALSE(placement_ok(m, SubCell{22, 22}, kLightSub, p)); // now covers cell 12, 12
    m.height.at(30, 30) = static_cast<std::uint8_t>(100 + p.flat_limit + 1);
    CHECK_FALSE(placement_ok(m, SubCell{58, 58}, kHeavySub, p)); // cells 29..31
    CHECK(placement_ok(m, SubCell{50, 50}, kHeavySub, p));
    CHECK_FALSE(placement_ok(m, SubCell{-1, 5}, kLightSub, p));        // off the subgrid
    CHECK_FALSE(placement_ok(m, SubCell{kSubW - 2, 5}, kLightSub, p)); // hangs off the map
    CHECK(placement_ok(m, SubCell{kSubW - 3, 5}, kLightSub, p));
}

TEST_CASE("walking distance goes around cliffs") {
    const GenParams p;
    Map m = make_fallback_map(1, p);
    const Rect core = cells_under(*m.core, kCoreSub); // the cells the core covers
    // A cliff wall (a raised ring) all round the core, 3 cells out: nothing outside can reach it.
    for (int y = core.y - 4; y <= core.y + core.h + 3; ++y) {
        for (int x = core.x - 4; x <= core.x + core.w + 3; ++x) {
            const bool ring = y == core.y - 4 || y == core.y + core.h + 3 || x == core.x - 4 ||
                              x == core.x + core.w + 3;
            if (ring) {
                m.height.at(x, y) = 180;
            }
        }
    }
    const Grid<int> dist = walk_distance_to_core(m, p);
    CHECK(dist.at(core.x - 1, core.y) == 0);
    CHECK(dist.at(core.x - 3, core.y) == 2);
    CHECK(dist.at(0, 0) < 0);
    CHECK(dist.at(core.x, core.y) < 0); // the core's own cells aren't walked
}

TEST_CASE("the fallback map is valid") {
    const GenParams p;
    const Map m = make_fallback_map(99, p);
    REQUIRE(m.core.has_value());
    CHECK(placement_ok(m, *m.core, kCoreSub, p));
    const Grid<int> dist = walk_distance_to_core(m, p);
    REQUIRE(m.spawns.size() == p.spawn_waves.size());
    for (const Spawn& s : m.spawns) {
        CHECK(dist.at(s.cell) >= p.min_spawn_dist);
        CHECK(s.on_edge);
    }
}

TEST_CASE("sweep: 1,000 seeds give valid maps without the fallback") {
    const GenParams p;
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
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
    long long cliff = 0;
    long long buildable = 0;
    int edge_spawns = 0;
    long long spawn_dist = 0;
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed) {
        const Map m = generate_map(seed, p);
        attempts += m.stats.attempts;
        max_attempts = std::max(max_attempts, m.stats.attempts);
        fallbacks += m.stats.fallback ? 1 : 0;
        const Grid<int> dist = walk_distance_to_core(m, p);
        for (const Spawn& s : m.spawns) {
            edge_spawns += s.on_edge ? 1 : 0;
            spawn_dist += dist.at(s.cell);
        }
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                formation += m.formation.at(x, y) != 0 ? 1 : 0;
                cliff += is_cliff_cell(m, {x, y}, p) ? 1 : 0;
                buildable +=
                    x % 2 == 0 && y % 2 == 0 && placement_ok(m, SubCell{x * 2, y * 2}, kLightSub, p)
                        ? 1
                        : 0;
            }
        }
    }
    const double cells = static_cast<double>(kSeeds) * kCells;
    MESSAGE("attempts avg ", static_cast<double>(attempts) / kSeeds, " max ", max_attempts,
            "  fallbacks ", fallbacks);
    MESSAGE("formation % ", 100.0 * static_cast<double>(formation) / cells, "  cliff % ",
            100.0 * static_cast<double>(cliff) / cells, "  light spots buildable % ",
            400.0 * static_cast<double>(buildable) / cells);
    MESSAGE("edge spawns % ", 100.0 * edge_spawns / (3.0 * kSeeds), "  spawn walk avg ",
            static_cast<double>(spawn_dist) / (3.0 * kSeeds));
}
