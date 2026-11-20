#include "core/overlay.hpp"

#include <algorithm>
#include <cstdint>

#include <doctest/doctest.h>

#include "core/mapgen.hpp"

using namespace pixeltd;

namespace {

// The same FNV-1a over the layers the golden test uses (material, formation, core, spawns), kept
// local so this test doesn't depend on test_mapgen.cpp.
std::uint64_t hash_map(const Map& m) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto mix = [&](std::uint64_t v) {
        h ^= v;
        h *= 0x100000001b3ull;
    };
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            mix(static_cast<std::uint64_t>(m.ground.at(x, y)));
            mix(m.rock.at(x, y));
        }
    }
    mix(m.core ? static_cast<std::uint64_t>(m.core->x * 1000 + m.core->y) : 0u); // subcells
    for (const Spawn& s : m.spawns) {
        mix(static_cast<std::uint64_t>(s.cell.x * 1000 + s.cell.y));
        mix(static_cast<std::uint64_t>(s.opens_at_wave));
    }
    return h;
}

} // namespace

TEST_CASE("recording the noise layers never changes the map") {
    const GenParams p;
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
        CAPTURE(seed);
        GenLayers layers;
        const Map with = generate_map(seed, p, &layers);
        const Map without = generate_map(seed, p);
        CHECK(hash_map(with) == hash_map(without));
        CHECK(layers.valid);
    }
}

TEST_CASE("the layers are the noise the map was made from") {
    const GenParams p;
    for (std::uint64_t seed : {1ull, 7ull, 0xC0FFEEull}) {
        CAPTURE(seed);
        GenLayers L;
        const Map m = generate_map(seed, p, &L);
        REQUIRE(L.valid);
        CHECK(L.fertility.width() == kMapW);
        CHECK(L.moisture.height() == kMapH);
        bool range_ok = true;
        bool ground_ok = true;
        bool trees_ok = true;
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                for (const Grid<int>* g : {&L.fertility, &L.moisture, &L.mountain}) {
                    range_ok = range_ok && g->at(x, y) >= 0 && g->at(x, y) <= 255;
                }
                const int f = L.fertility.at(x, y) * 100 / 255;
                const Ground g = m.ground.at(x, y);
                // Sand and rich soil come from fertility (where the ground isn't wet or gravel).
                if (m.rock.at(x, y) == 0 && g == Ground::Sand) {
                    ground_ok = ground_ok && f < p.sand_fertility_pct;
                }
                if (m.rock.at(x, y) == 0 && g == Ground::RichSoil) {
                    ground_ok = ground_ok && f >= p.rich_fertility_pct;
                }
                if (m.plant.at(x, y) == Plant::Tree) {
                    trees_ok = trees_ok && (g == Ground::Soil || g == Ground::RichSoil) &&
                               m.rock.at(x, y) == 0;
                }
            }
        }
        CHECK(range_ok);
        CHECK(ground_ok);
        CHECK(trees_ok);
    }
}

TEST_CASE("the fallback map has no noise") {
    GenParams p;
    p.attempts = 1;
    p.min_spawn_dist = 100000; // no spawn can ever be placed
    GenLayers L;
    L.valid = true;
    const Map m = generate_map(5, p, &L);
    CHECK(m.stats.fallback);
    CHECK_FALSE(L.valid);
}

TEST_CASE("the O key steps through every view and wraps both ways") {
    Overlay o = Overlay::Off;
    for (int i = 1; i <= kOverlayCount; ++i) {
        o = step_overlay(o, 1);
        CHECK(static_cast<int>(o) == i % kOverlayCount);
    }
    CHECK(step_overlay(Overlay::Off, -1) == Overlay::Moisture);
    CHECK(step_overlay(Overlay::Mountain, -1) == Overlay::Off);
    CHECK(step_overlay(Overlay::Moisture, 1) == Overlay::Off);
}

TEST_CASE("overlay names round-trip, and unknown names are refused") {
    for (int i = 0; i < kOverlayCount; ++i) {
        const auto o = static_cast<Overlay>(i);
        Overlay parsed = Overlay::Off;
        CHECK(parse_overlay(overlay_key(o), parsed));
        CHECK(parsed == o);
        CHECK(overlay_label(o)[0] != '\0');
    }
    Overlay parsed = Overlay::Mountain;
    CHECK_FALSE(parse_overlay("elevation", parsed)); // gone with the heightmap (#30)
    CHECK_FALSE(parse_overlay("rough", parsed));     // gone with the formations (#32)
    CHECK(parsed == Overlay::Mountain);
}
