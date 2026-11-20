#include "core/fog.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

#include <doctest/doctest.h>

#include "core/mapgen.hpp"
#include "core/terrain.hpp"

using namespace pixeltd;

namespace {

int count_explored(const Fog& fog) {
    const auto& c = fog.cells().cells();
    return static_cast<int>(std::count(c.begin(), c.end(), std::uint8_t{1}));
}

} // namespace

TEST_CASE("run start explores the ground around the core") {
    Map m = make_fallback_map(1);
    m.core = SubCell{300, 160}; // px 2400..2472 x 1280..1352, cells 150..154 x 80..84
    Fog fog;
    const Rect box = fog.explore_core(m, 72);
    CHECK(fog.explored({150, 80})); // under the core
    CHECK(fog.explored({154, 84}));
    // Left: cell 146's centre is 2344, 56 px from the core's edge; cell 145's is 72 px (still in);
    // cell 144's is 88 px.
    CHECK(fog.explored({146, 82}));
    CHECK(fog.explored({145, 82}));
    CHECK_FALSE(fog.explored({144, 82}));
    // Diagonally: cell (146, 76) is 56 px left and 56 px up, 79 px away.
    CHECK_FALSE(fog.explored({146, 76}));
    CHECK(fog.explored({147, 77})); // 40 and 40: 57 px
    CHECK_FALSE(fog.explored({10, 10}));
    CHECK(box.contains(Rect{146, 76, 13, 13}));
}

TEST_CASE("towers explore what they can see, within a circle") {
    Map m = make_fallback_map(1);
    const PointPx tower = centre_of(Cell{10, 10});
    const int range = 8 * kCellPx;

    SUBCASE("open ground: a circle of its range") {
        Fog fog;
        fog.explore_from(m, tower, range);
        CHECK(fog.explored({18, 10})); // 128 px: on the edge
        CHECK_FALSE(fog.explored({19, 10}));
        CHECK(fog.explored({15, 15}));       // 80 and 80: 113 px
        CHECK_FALSE(fog.explored({16, 16})); // 96 and 96: 136 px
    }
    SUBCASE("a wall of rock blocks its sight") {
        for (int y = 0; y < kMapH; ++y) {
            m.rock.at(15, y) = 120;
        }
        Fog fog;
        fog.explore_from(m, tower, range);
        CHECK(fog.explored({12, 10}));
        CHECK(fog.explored({15, 10})); // the rock face itself is seen
        CHECK_FALSE(fog.explored({17, 10}));
        CHECK(fog.explored({10, 3})); // its own side, other ways
    }
    SUBCASE("a single rock casts a sight shadow") {
        m.rock.at(13, 10) = 1;
        Fog fog;
        fog.explore_from(m, tower, range);
        CHECK_FALSE(fog.explored({16, 10})); // straight behind it
        CHECK(fog.explored({16, 14}));       // off to the side
    }
}

TEST_CASE("explored ground stays explored; spawns and enemies are hidden until then") {
    Map m = make_fallback_map(1);
    Fog fog;
    const Rect first = fog.explore_from(m, centre_of(Cell{30, 30}), 6 * kCellPx);
    const int after_first = count_explored(fog);
    CHECK(after_first > 0);
    CHECK(first.w > 0);
    const Rect again = fog.explore_from(m, centre_of(Cell{30, 30}), 6 * kCellPx);
    CHECK(again.w == 0);
    CHECK(count_explored(fog) == after_first);
    fog.explore_from(m, centre_of(Cell{36, 30}), 6 * kCellPx);
    CHECK(count_explored(fog) > after_first);
    CHECK(fog.visible({30, 30}));
    CHECK_FALSE(fog.visible({200, 150}));
    CHECK_FALSE(fog.visible({-1, 0}));
    const Spawn s{{60, 30}, false, 1};
    CHECK_FALSE(fog.spawn_found(s));
    fog.explore_from(m, centre_of(Cell{61, 31}), 2 * kCellPx);
    CHECK(fog.spawn_found(s));
}

TEST_CASE("a real map: the core's surroundings are explored and the spawns hidden at start") {
    const GenParams p;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        CAPTURE(seed);
        const Map m = generate_map(seed, p);
        Fog fog;
        fog.explore_core(m, p.light_range_px);
        REQUIRE(m.core.has_value());
        const Rect core = cells_under(*m.core, kCoreSub);
        CHECK(fog.explored({core.x, core.y}));
        CHECK(fog.explored({core.x - 1, core.y}));
        for (const Spawn& s : m.spawns) {
            CHECK_FALSE(fog.spawn_found(s));
        }
    }
}

// Manual timing (Release on ANODE-MGMT): pixeltd_tests -tc="*bench*" --no-skip
TEST_CASE("generation bench (manual)" * doctest::skip()) {
    using clock = std::chrono::steady_clock;
    const GenParams p;
    constexpr int kMaps = 100;
    double gen_total = 0.0;
    double gen_max = 0.0;
    double fog_total = 0.0;
    double fog_max = 0.0;
    for (std::uint64_t seed = 0; seed < kMaps; ++seed) {
        const auto t0 = clock::now();
        const Map m = generate_map(seed, p);
        const auto t1 = clock::now();
        Fog fog;
        fog.explore_core(m, p.light_range_px);
        for (int i = 0; i < 8; ++i) {
            const SubCell c{m.core->x - 40 + 12 * i, m.core->y + 20};
            fog.explore_from(m, centre_of(c, kLightSub), p.light_range_px);
        }
        const auto t2 = clock::now();
        const double g = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double f = std::chrono::duration<double, std::milli>(t2 - t1).count();
        gen_total += g;
        fog_total += f;
        gen_max = std::max(gen_max, g);
        fog_max = std::max(fog_max, f);
    }
    MESSAGE("generate_map avg ", gen_total / kMaps, " ms, max ", gen_max, " ms");
    // The same with every layer domain-warped.
    GenParams heavy = p;
    for (NoiseSpec* n : {&heavy.fertility_noise, &heavy.moisture_noise, &heavy.mountain_noise,
                         &heavy.forest_noise}) {
        n->warp_cells = 16;
    }
    double heavy_total = 0.0;
    for (std::uint64_t seed = 0; seed < 30; ++seed) {
        const auto t0 = clock::now();
        const Map m = generate_map(seed, heavy);
        heavy_total += std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        CHECK(m.core.has_value());
    }
    MESSAGE("generate_map, warp on every layer: avg ", heavy_total / 30, " ms");
    MESSAGE("fog (core + 8 viewers) avg ", fog_total / kMaps, " ms, max ", fog_max, " ms");
}
