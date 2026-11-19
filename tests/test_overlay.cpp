#include "core/overlay.hpp"

#include <algorithm>
#include <cstdint>

#include <doctest/doctest.h>

#include "core/mapgen.hpp"

using namespace pixeltd;

namespace {

// The same FNV-1a over the layers the golden test uses (height, material, formation, core,
// spawns), kept local so this test doesn't depend on test_mapgen.cpp.
std::uint64_t hash_map(const Map& m) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    const auto mix = [&](std::uint64_t v) {
        h ^= v;
        h *= 0x100000001b3ull;
    };
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            mix(m.height.at(x, y));
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
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
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
        CHECK(L.height_sum.width() == kMapW);
        CHECK(L.height_sum.height() == kMapH);
        const auto [lo_it, hi_it] =
            std::minmax_element(L.height_sum.cells().begin(), L.height_sum.cells().end());
        const int lo = *lo_it;
        const int span = std::max(1, *hi_it - lo);
        const int band = p.mesa_band;
        int mesa_cells = 0;
        bool sums_ok = true;
        bool heights_ok = true;
        bool materials_ok = true;
        bool rocks_ok = true;
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                for (const auto& o : L.octave) {
                    sums_ok = sums_ok && o.at(x, y) >= 0 && o.at(x, y) <= 255;
                }
                // The default height noise has 3 octaves (weights 4 : 2 : 1).
                sums_ok = sums_ok &&
                          L.height_sum.at(x, y) == L.octave_weight[0] * L.octave[0].at(x, y) +
                                                       L.octave_weight[1] * L.octave[1].at(x, y) +
                                                       L.octave_weight[2] * L.octave[2].at(x, y);
                materials_ok = materials_ok && static_cast<int>(m.material.at(x, y)) ==
                                                   L.material.at(x, y) * kMaterials / 256;
                mesa_cells += L.mesa.at(x, y);
                if (m.formation.at(x, y) != 0) {
                    // Formation heights come from the roughness.
                    rocks_ok =
                        rocks_ok && m.height.at(x, y) ==
                                        p.formation_height_min +
                                            L.rough.at(x, y) * (255 - p.formation_height_min) / 255;
                    continue;
                }
                // Ground heights: the stretched sum, blended towards the terraced table (level,
                // centred, plus a compressed copy of the slope) by the feather.
                const int smooth = static_cast<int>(
                    static_cast<long long>(L.height_sum.at(x, y) - lo) * p.ground_max / span);
                int h = smooth;
                if (L.mesa.at(x, y) != 0) {
                    const int terraced =
                        std::min(p.ground_max, smooth / band * band + (band - p.mesa_relief) / 2 +
                                                   smooth % band * p.mesa_relief / band);
                    h = smooth + (terraced - smooth) * L.mesa_blend.at(x, y) / 256;
                } else {
                    heights_ok = heights_ok && L.mesa_blend.at(x, y) == 0;
                }
                heights_ok = heights_ok && m.height.at(x, y) == h;
            }
        }
        CHECK(sums_ok);
        CHECK(heights_ok);
        CHECK(materials_ok);
        CHECK(rocks_ok);
        CHECK(mesa_cells >= kMapW * kMapH * p.mesa_share_pct / 100);
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
    CHECK(step_overlay(Overlay::Elevation, -1) == Overlay::Off);
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
    Overlay parsed = Overlay::Mesa;
    CHECK_FALSE(parse_overlay("heightmap", parsed));
    CHECK(parsed == Overlay::Mesa);
}

TEST_CASE("the elevation ramp hits its stops and formations are pale") {
    // One stop every 20 units.
    CHECK(elevation_colour(0) == Rgb8{24, 40, 110});    // deep blue
    CHECK(elevation_colour(20) == Rgb8{28, 72, 150});   // blue
    CHECK(elevation_colour(40) == Rgb8{32, 128, 140});  // teal
    CHECK(elevation_colour(60) == Rgb8{48, 150, 110});  // sea green
    CHECK(elevation_colour(80) == Rgb8{70, 160, 70});   // green
    CHECK(elevation_colour(100) == Rgb8{150, 185, 70}); // yellow-green
    CHECK(elevation_colour(120) == Rgb8{220, 205, 80}); // yellow
    CHECK(elevation_colour(140) == Rgb8{228, 175, 62}); // amber
    CHECK(elevation_colour(160) == Rgb8{225, 140, 50}); // orange
    CHECK(elevation_colour(180) == Rgb8{195, 98, 44});  // red-orange
    CHECK(elevation_colour(200) == Rgb8{150, 60, 40});  // red-brown
    CHECK(elevation_colour(10) == Rgb8{26, 56, 130});   // halfway, deep blue to blue
    CHECK(elevation_colour(-5) == elevation_colour(0));
    for (int h : {210, 230, 255}) {
        const Rgb8 c = elevation_colour(h);
        CHECK(c.r >= 196);
        CHECK(c.r == c.g);
        CHECK(c.b == c.r + 10);
    }
    CHECK(elevation_colour(255).r > elevation_colour(210).r);
}

TEST_CASE("contours: minor every 10 units, major every 50") {
    CHECK(contour_level(5 * 16, 8 * 16) == 0);
    CHECK(contour_level(9 * 16, 10 * 16) == 1);
    CHECK(contour_level(10 * 16, 9 * 16 + 15) == 1); // either order
    CHECK(contour_level(49 * 16, 50 * 16) == 2);
    CHECK(contour_level(30 * 16, 70 * 16) == 2); // a cliff crossing many lines
    CHECK(contour_level(50 * 16 + 1, 59 * 16) == 0);
}
