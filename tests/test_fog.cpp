#include "core/fog.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "core/mapgen.hpp"

using namespace pixeltd;

namespace {

int count_explored(const Fog& fog) {
    const auto& c = fog.cells().cells();
    return static_cast<int>(std::count(c.begin(), c.end(), std::uint8_t{1}));
}

void fill(Map& m, Rect r, Terrain t) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            m.terrain.at(x, y) = t;
        }
    }
}

} // namespace

TEST_CASE("placing the core explores the ground around it, and its ramp") {
    Map m = make_fallback_map(1);
    m.core = Cell{100, 20};
    // A ramp cell diagonally 6 cells from the core: 8.5 cells away, but the ramp always counts.
    m.path.at(100 + kCoreSize - 1 + 6, 20 + kCoreSize - 1 + 6) = PathKind::Ramp;
    Fog fog;
    const Rect box = fog.explore_core(m, 6);
    CHECK(fog.explored({100, 20}));               // the core itself
    CHECK(fog.explored({100 - 6, 22}));           // 6 cells left of it
    CHECK_FALSE(fog.explored({100 - 7, 22}));     // 7 cells left
    CHECK_FALSE(fog.explored({100 - 5, 20 - 5})); // diagonally 7.1 cells away
    CHECK(fog.explored({100 + kCoreSize - 1 + 6, 20 + kCoreSize - 1 + 6}));
    CHECK_FALSE(fog.explored({10, 10}));
    CHECK(box.contains(Cell{100, 20}));
    CHECK(box.contains(Rect{94, 14, 18, 18}));
}

TEST_CASE("towers explore what they can see, from their height") {
    Map m = make_fallback_map(1);
    fill(m, {15, 0, 1, kMapH}, Terrain::Rock); // a rock wall at x = 15
    const PointPx tower = centre_of(Cell{10, 10});
    const int range = 10 * kCellPx;

    SUBCASE("low ground: the rocks block its sight") {
        Fog fog;
        fog.explore_from(m, tower, range, Height::Low);
        CHECK(fog.explored({12, 10}));
        CHECK_FALSE(fog.explored({18, 10}));
        CHECK_FALSE(fog.explored({25, 10})); // out of range anyway
    }
    SUBCASE("high ground sees over them") {
        Fog fog;
        fog.explore_from(m, tower, range, Height::High);
        CHECK(fog.explored({18, 10}));
        CHECK_FALSE(fog.explored({21, 10})); // 11 cells: out of range
    }
    SUBCASE("cliffs block high ground too") {
        fill(m, {15, 0, 1, kMapH}, Terrain::Cliff);
        Fog fog;
        fog.explore_from(m, tower, range, Height::High);
        CHECK(fog.explored({12, 10}));
        CHECK_FALSE(fog.explored({18, 10}));
    }
}

TEST_CASE("explored ground stays explored") {
    Map m = make_fallback_map(1);
    Fog fog;
    const Rect first = fog.explore_from(m, centre_of(Cell{30, 30}), 6 * kCellPx, Height::Low);
    const int after_first = count_explored(fog);
    CHECK(after_first > 0);
    CHECK(first.w > 0);
    // The same viewer again explores nothing new; a second viewer only adds.
    const Rect again = fog.explore_from(m, centre_of(Cell{30, 30}), 6 * kCellPx, Height::Low);
    CHECK(again.w == 0);
    CHECK(count_explored(fog) == after_first);
    fog.explore_from(m, centre_of(Cell{36, 30}), 6 * kCellPx, Height::Low);
    CHECK(count_explored(fog) > after_first);
    CHECK(fog.explored({30, 30}));
    // Enemies are hidden exactly where the ground is unexplored.
    CHECK(fog.visible({30, 30}));
    CHECK_FALSE(fog.visible({100, 50}));
    CHECK_FALSE(fog.visible({-1, 0}));
}

TEST_CASE("a real map: the core's surroundings and ramp are explored at once") {
    const GenParams p;
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        Map m = generate_stage1(seed, p);
        REQUIRE(generate_stage2(m, *central_core_spot(m), p));
        Fog fog;
        fog.explore_core(m, p.light_range_cells);
        CAPTURE(seed);
        // The path's last cell (beside the core) is explored; the spawn is too far to be.
        CHECK(fog.explored(m.spine.back()));
        CHECK_FALSE(fog.explored(m.spawn));
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                if (m.path.at(x, y) == PathKind::Ramp) {
                    REQUIRE(fog.explored({x, y}));
                }
            }
        }
    }
}

// Manual timing (Release on ANODE-MGMT): pixeltd_tests -tc="*bench*" --no-skip
TEST_CASE("generation bench (manual)" * doctest::skip()) {
    using clock = std::chrono::steady_clock;
    const GenParams p;
    constexpr int kMaps = 200;
    double s1_total = 0.0;
    double s1_max = 0.0;
    double s2_total = 0.0;
    double s2_max = 0.0;
    double ex_total = 0.0;
    double ex_max = 0.0;
    for (std::uint64_t seed = 0; seed < kMaps; ++seed) {
        auto t0 = clock::now();
        Map m = generate_stage1(seed, p);
        auto t1 = clock::now();
        generate_stage2(m, *central_core_spot(m), p);
        auto t2 = clock::now();
        Fog fog;
        fog.explore_core(m, p.light_range_cells);
        const int range = p.light_range_cells * kCellPx;
        for (std::size_t i = 0; i < m.spine.size(); i += 40) {
            fog.explore_from(m, centre_of(m.spine[i]), range, Height::Low);
        }
        auto t3 = clock::now();
        const double s1 = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double s2 = std::chrono::duration<double, std::milli>(t2 - t1).count();
        const double ex = std::chrono::duration<double, std::milli>(t3 - t2).count();
        s1_total += s1;
        s2_total += s2;
        ex_total += ex;
        s1_max = std::max(s1_max, s1);
        s2_max = std::max(s2_max, s2);
        ex_max = std::max(ex_max, ex);
    }
    MESSAGE("stage 1 avg ", s1_total / kMaps, " ms, max ", s1_max, " ms");
    MESSAGE("stage 2 avg ", s2_total / kMaps, " ms, max ", s2_max, " ms");
    MESSAGE("fog (core + ~8 viewers) avg ", ex_total / kMaps, " ms, max ", ex_max, " ms");
}
