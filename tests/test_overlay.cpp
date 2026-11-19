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
            mix(static_cast<std::uint64_t>(m.material.at(x, y)));
            mix(m.formation.at(x, y));
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
        CHECK(L.material.width() == kMapW);
        CHECK(L.material.height() == kMapH);
        bool materials_ok = true;
        bool rocks_ok = true;
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                materials_ok = materials_ok && static_cast<int>(m.material.at(x, y)) ==
                                                   L.material.at(x, y) * kMaterials / 256;
                if (m.formation.at(x, y) != 0) {
                    // A formation's looks value comes from the roughness.
                    rocks_ok = rocks_ok && m.formation.at(x, y) == 1 + L.rough.at(x, y) * 254 / 255;
                }
            }
        }
        CHECK(materials_ok);
        CHECK(rocks_ok);
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
    CHECK(step_overlay(Overlay::Off, -1) == Overlay::Material);
    CHECK(step_overlay(Overlay::Rough, -1) == Overlay::Off);
    CHECK(step_overlay(Overlay::Material, 1) == Overlay::Off);
}

TEST_CASE("overlay names round-trip, and unknown names are refused") {
    for (int i = 0; i < kOverlayCount; ++i) {
        const auto o = static_cast<Overlay>(i);
        Overlay parsed = Overlay::Off;
        CHECK(parse_overlay(overlay_key(o), parsed));
        CHECK(parsed == o);
        CHECK(overlay_label(o)[0] != '\0');
    }
    Overlay parsed = Overlay::Rough;
    CHECK_FALSE(parse_overlay("elevation", parsed)); // gone with the heightmap (#30)
    CHECK(parsed == Overlay::Rough);
}
