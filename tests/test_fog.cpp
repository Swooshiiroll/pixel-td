#include "core/fog.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

#include <doctest/doctest.h>

#include "core/mapgen.hpp"

using namespace pixeltd;

namespace {

int count_explored(const Fog& fog) {
    const auto& c = fog.cells().cells();
    return static_cast<int>(std::count(c.begin(), c.end(), std::uint8_t{1}));
}

void fill(Map& m, Rect r, int height) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            m.height.at(x, y) = static_cast<std::uint8_t>(height);
        }
    }
}

} // namespace

TEST_CASE("run start explores the ground around the core") {
    const GenParams p;
    Map m = make_fallback_map(1, p);
    m.core = Cell{150, 80};
    Fog fog;
    const Rect box = fog.explore_core(m, 6);
    CHECK(fog.explored({150, 80}));               // the core itself
    CHECK(fog.explored({150 - 6, 82}));           // 6 cells left of it
    CHECK_FALSE(fog.explored({150 - 7, 82}));     // 7 cells left
    CHECK_FALSE(fog.explored({150 - 5, 80 - 5})); // diagonally 7.1 cells away
    CHECK_FALSE(fog.explored({10, 10}));
    CHECK(box.contains(Rect{144, 74, 18, 18}));
}

TEST_CASE("towers explore what they can see over the terrain") {
    const GenParams p;
    Map m = make_fallback_map(1, p);
    fill(m, {15, 0, 1, kMapH}, 120); // a ridge at x = 15
    const PointPx tower = centre_of(Cell{10, 10});
    const int base = 8 * kCellPx;

    SUBCASE("low ground: the ridge blocks its sight") {
        Fog fog;
        fog.explore_from(m, tower, base, p);
        CHECK(fog.explored({12, 10}));
        CHECK_FALSE(fog.explored({17, 10}));
    }
    SUBCASE("high ground sees over it, and further") {
        fill(m, {9, 9, 3, 3}, 160);
        Fog fog;
        fog.explore_from(m, tower, base, p);
        // (The ground right at the plateau's foot is dead ground: the edge hides it.)
        CHECK(fog.explored({17, 10}));
        // 60 units above the ground beyond: +30% reach, so 10 cells (8 * 1.3 = 10.4).
        CHECK(fog.explored({20, 10}));
        CHECK_FALSE(fog.explored({22, 10}));
    }
    SUBCASE("rock formations block high ground too") {
        fill(m, {9, 9, 3, 3}, 160);
        for (int y = 0; y < kMapH; ++y) {
            m.formation.at(15, y) = 1;
        }
        Fog fog;
        fog.explore_from(m, tower, base, p);
        CHECK(fog.explored({11, 10})); // its own plateau
        CHECK_FALSE(fog.explored({17, 10}));
    }
}

TEST_CASE("explored ground stays explored; spawns and enemies are hidden until then") {
    const GenParams p;
    Map m = make_fallback_map(1, p);
    Fog fog;
    const Rect first = fog.explore_from(m, centre_of(Cell{30, 30}), 6 * kCellPx, p);
    const int after_first = count_explored(fog);
    CHECK(after_first > 0);
    CHECK(first.w > 0);
    const Rect again = fog.explore_from(m, centre_of(Cell{30, 30}), 6 * kCellPx, p);
    CHECK(again.w == 0);
    CHECK(count_explored(fog) == after_first);
    fog.explore_from(m, centre_of(Cell{36, 30}), 6 * kCellPx, p);
    CHECK(count_explored(fog) > after_first);
    CHECK(fog.visible({30, 30}));
    CHECK_FALSE(fog.visible({200, 150}));
    CHECK_FALSE(fog.visible({-1, 0}));
    const Spawn s{{60, 30}, false, 1};
    CHECK_FALSE(fog.spawn_found(s));
    fog.explore_from(m, centre_of(Cell{61, 31}), 2 * kCellPx, p);
    CHECK(fog.spawn_found(s));
}

TEST_CASE("a real map: the core's surroundings are explored and the spawns hidden at start") {
    const GenParams p;
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        CAPTURE(seed);
        const Map m = generate_map(seed, p);
        Fog fog;
        fog.explore_core(m, p.light_range_cells);
        REQUIRE(m.core.has_value());
        CHECK(fog.explored(*m.core));
        CHECK(fog.explored({m.core->x - 1, m.core->y}));
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
        fog.explore_core(m, p.light_range_cells);
        for (int i = 0; i < 8; ++i) {
            const Cell c{m.core->x - 20 + 6 * i, m.core->y + 10};
            fog.explore_from(m, centre_of(c, kLightSize), p.light_range_cells * kCellPx, p);
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
    MESSAGE("fog (core + 8 viewers) avg ", fog_total / kMaps, " ms, max ", fog_max, " ms");
}
