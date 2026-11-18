#include "core/mapgen.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "core/hash.hpp"

using namespace pixeltd;

namespace {

std::uint64_t map_hash(const Map& m) {
    Fnv1a h;
    for (const Terrain t : m.terrain.cells()) {
        h.add_byte(static_cast<std::uint8_t>(t));
    }
    h.add_int(m.zone.x);
    h.add_int(m.zone.y);
    h.add_int(m.zone.w);
    h.add_int(m.zone.h);
    return h.value();
}

int count_terrain(const Map& m, Terrain t) {
    return static_cast<int>(std::count(m.terrain.cells().begin(), m.terrain.cells().end(), t));
}

void fill(Map& m, Rect r, Terrain t) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            m.terrain.at(x, y) = t;
        }
    }
}

constexpr int kCells = kMapW * kMapH;

} // namespace

TEST_CASE("stage 1 is deterministic") {
    const Map a = generate_stage1(42);
    const Map b = generate_stage1(42);
    CHECK(a.terrain == b.terrain);
    CHECK(a.zone == b.zone);
    CHECK(a.stats.stage1_attempts == b.stats.stage1_attempts);
    CHECK(a.stats.zone_tries == b.stats.zone_tries);
}

TEST_CASE("different seeds give different maps") {
    std::vector<std::uint64_t> hashes;
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        hashes.push_back(map_hash(generate_stage1(seed)));
    }
    std::sort(hashes.begin(), hashes.end());
    CHECK(std::adjacent_find(hashes.begin(), hashes.end()) == hashes.end());
}

// Pinned results: these must match on every compiler and platform (MSVC locally, GCC on CI). If
// the generator is changed on purpose, update the values in the same pull request and say why.
TEST_CASE("golden stage 1 maps") {
    CHECK(map_hash(generate_stage1(1)) == 0x8b3d1da0930fa823ull);
    CHECK(map_hash(generate_stage1(0xC0FFEEull)) == 0x98f4b9003855eab7ull);
    CHECK(map_hash(generate_stage1(0xFFFFFFFFFFFFFFFFull)) == 0x9f8bc8d5c20e2682ull);
}

TEST_CASE("core spots must be inside the zone, on one height, off blockers") {
    Map m = make_fallback_map(7);
    const Cell s{m.zone.x, m.zone.y};
    CHECK(is_legal_core_spot(m, s));
    CHECK_FALSE(is_legal_core_spot(m, {m.zone.x - 1, m.zone.y}));
    CHECK_FALSE(is_legal_core_spot(m, {m.zone.x + m.zone.w - kCoreSize + 1, m.zone.y}));
    CHECK(
        is_legal_core_spot(m, {m.zone.x + m.zone.w - kCoreSize, m.zone.y + m.zone.h - kCoreSize}));

    SUBCASE("a rock in the footprint") {
        m.terrain.at(s.x + 3, s.y + 2) = Terrain::Rock;
        CHECK_FALSE(is_legal_core_spot(m, s));
    }
    SUBCASE("straddling low and high ground") {
        fill(m, {s.x, s.y, 3, kCoreSize}, Terrain::High);
        CHECK_FALSE(is_legal_core_spot(m, s));
    }
    SUBCASE("all high ground") {
        fill(m, {s.x, s.y, kCoreSize, kCoreSize}, Terrain::High);
        CHECK(is_legal_core_spot(m, s));
    }
}

TEST_CASE("path brushes fit only on ground") {
    Map m = make_fallback_map(1);
    m.terrain.at(5, 5) = Terrain::Rock;
    const BrushFit fit(m.terrain);
    CHECK(fit.fits({0, 0}, 2));
    CHECK(fit.fits({kMapW - 2, 10}, 2));
    CHECK_FALSE(fit.fits({kMapW - 1, 10}, 2)); // would hang off the map
    CHECK_FALSE(fit.fits({4, 4}, 2));          // covers (5, 5)
    CHECK(fit.fits({3, 3}, 2));
    CHECK_FALSE(fit.fits({5, 5}, 3)); // anchored at (4, 4): covers the rock
    CHECK(fit.fits({20, 20}, 6));
    CHECK(fit.square_at(20, 20) == BrushFit::kMaxBrush);
    CHECK(fit.square_at(0, 0) == 5); // the rock at (5, 5) caps it
}

TEST_CASE("reachability") {
    const GenParams p;
    Map m = make_fallback_map(3);
    const Cell s{m.zone.x + 4, m.zone.y + 4};

    SUBCASE("open ground routes") {
        const Reachability reach(m.terrain, p);
        CHECK(reach.routable(s));
        CHECK(reach.farthest_edge_from(m.zone) >= p.min_spawn_dist);
    }
    SUBCASE("a core walled in by cliffs doesn't") {
        fill(m, {s.x - 2, s.y - 2, kCoreSize + 4, kCoreSize + 4}, Terrain::Cliff);
        fill(m, {s.x - 1, s.y - 1, kCoreSize + 2, kCoreSize + 2}, Terrain::Ground);
        const Reachability reach(m.terrain, p);
        CHECK_FALSE(reach.routable(s));
    }
    SUBCASE("a high-ground core routes down a ramp near the plateau edge") {
        // Plateau from s.x - 2: the core's left edge is 2 cells from low ground.
        fill(m, {s.x - 2, s.y - 8, kCoreSize + 20, kCoreSize + 16}, Terrain::High);
        const Reachability reach(m.terrain, p);
        CHECK(reach.routable(s));
    }
    SUBCASE("but not from deep inside a plateau") {
        const int r = p.ramp_max;
        fill(m, {s.x - r - 3, s.y - r - 3, kCoreSize + 2 * r + 6, kCoreSize + 2 * r + 6},
             Terrain::High);
        const Reachability reach(m.terrain, p);
        CHECK_FALSE(reach.routable(s));
    }
}

TEST_CASE("the fallback map is valid") {
    const GenParams p;
    const Map m = make_fallback_map(99, p);
    CHECK(Rect{0, 0, kMapW, kMapH}.contains(m.zone));
    const Reachability reach(m.terrain, p);
    const std::vector<Cell> spots = legal_core_spots(m);
    CHECK(static_cast<int>(spots.size()) >= p.min_legal_spots);
    for (const Cell s : spots) {
        CHECK(reach.routable(s));
    }
    CHECK(reach.farthest_edge_from(m.zone) >= p.min_spawn_dist);
}

TEST_CASE("stage 1 sweep: 1,000 seeds give valid maps without the fallback") {
    const GenParams p;
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
        CAPTURE(seed);
        const Map m = generate_stage1(seed, p);
        REQUIRE_FALSE(m.stats.stage1_fallback);

        const int high = count_terrain(m, Terrain::High);
        const int cliff = count_terrain(m, Terrain::Cliff);
        REQUIRE(high * 100 >= p.high_final_min_pct * kCells);
        REQUIRE(high * 100 <= p.high_final_max_pct * kCells);
        REQUIRE(cliff * 100 <= p.cliff_final_max_pct * kCells);
        REQUIRE(count_terrain(m, Terrain::Rock) > 0);

        REQUIRE(m.zone.w == p.zone_w);
        REQUIRE(m.zone.h == p.zone_h);
        REQUIRE(Rect{0, 0, kMapW, kMapH}.contains(m.zone));

        const std::vector<Cell> spots = legal_core_spots(m);
        REQUIRE(static_cast<int>(spots.size()) >= p.min_legal_spots);
        const Reachability reach(m.terrain, p);
        for (const Cell s : spots) {
            REQUIRE(reach.routable(s));
        }
        REQUIRE(reach.farthest_edge_from(m.zone) >= p.min_spawn_dist);
    }
}

// Manual tuning aid: doctest pixeltd_tests -tc="*stats*" --no-skip
TEST_CASE("stage 1 stats (manual)" * doctest::skip()) {
    const GenParams p;
    int attempts = 0;
    int max_attempts = 0;
    int tries = 0;
    long long high = 0;
    long long cliff = 0;
    long long rock = 0;
    long long spots = 0;
    long long high_cores = 0;
    constexpr int kSeeds = 1000;
    for (std::uint64_t seed = 0; seed < kSeeds; ++seed) {
        const Map m = generate_stage1(seed, p);
        attempts += m.stats.stage1_attempts;
        max_attempts = std::max(max_attempts, m.stats.stage1_attempts);
        tries += m.stats.zone_tries;
        high += count_terrain(m, Terrain::High);
        cliff += count_terrain(m, Terrain::Cliff);
        rock += count_terrain(m, Terrain::Rock);
        const std::vector<Cell> s = legal_core_spots(m);
        spots += static_cast<long long>(s.size());
        for (const Cell c : s) {
            high_cores += m.terrain.at(c) == Terrain::High ? 1 : 0;
        }
    }
    MESSAGE("attempts avg ", static_cast<double>(attempts) / kSeeds, " max ", max_attempts);
    MESSAGE("zone tries avg ", static_cast<double>(tries) / kSeeds);
    MESSAGE("high % ", 100.0 * static_cast<double>(high) / (kSeeds * kCells), "  cliff % ",
            100.0 * static_cast<double>(cliff) / (kSeeds * kCells), "  rock % ",
            100.0 * static_cast<double>(rock) / (kSeeds * kCells));
    MESSAGE("legal spots avg ", static_cast<double>(spots) / kSeeds, "  on high ground % ",
            100.0 * static_cast<double>(high_cores) / static_cast<double>(spots));
}
