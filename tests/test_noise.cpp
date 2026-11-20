#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

#include <doctest/doctest.h>

#include "core/gen_params.hpp"
#include "core/gen_util.hpp"
#include "core/grid.hpp"
#include "core/mapgen.hpp"
#include "core/rng.hpp"
#include "core/terrain.hpp"

using namespace pixeltd;
using namespace pixeltd::gen;

namespace {

constexpr int kW = 320;
constexpr int kH = 180;

bool in_byte_range(const Grid<int>& g) {
    const auto [lo, hi] = std::minmax_element(g.cells().begin(), g.cells().end());
    return *lo >= 0 && *hi <= 255;
}

} // namespace

TEST_CASE("default noise is exactly the noise before #21") {
    for (std::uint64_t seed : {1ull, 99ull}) {
        CAPTURE(seed);
        // The old height: three octaves (64, 32, 16), weights 4 : 2 : 1, added in order.
        Rng a(seed, 7);
        std::array<Grid<int>, 3> old{{{kW, kH, 0}, {kW, kH, 0}, {kW, kH, 0}}};
        add_value_noise(old[0], a, 64, 1);
        add_value_noise(old[1], a, 32, 1);
        add_value_noise(old[2], a, 16, 1);
        Rng b(seed, 7);
        Grid<int> sum(kW, kH, 0);
        const int total = fill_noise(sum, b, NoiseSpec{64, 3});
        CHECK(total == 4096 + 2048 + 1024);
        bool same = true;
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                same = same && sum.at(x, y) == 1024 * (4 * old[0].at(x, y) + 2 * old[1].at(x, y) +
                                                       old[2].at(x, y));
            }
        }
        CHECK(same);
        CHECK(a.next_u32() == b.next_u32()); // the same number of draws

        // A single-octave layer (mesa, rough, material) divides back to exactly the old values.
        Rng c(seed, 8);
        Grid<int> old1(kW, kH, 0);
        add_value_noise(old1, c, 48, 1);
        Rng d(seed, 8);
        Grid<int> one(kW, kH, 0);
        const int w = fill_noise(one, d, NoiseSpec{48});
        bool same1 = true;
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                same1 = same1 && one.at(x, y) / w == old1.at(x, y);
            }
        }
        CHECK(same1);
        CHECK(c.next_u32() == d.next_u32());
    }
}

TEST_CASE("every curve eases from 0 to nearly 256 and stays in range") {
    for (const Curve curve : {Curve::Linear, Curve::Smoothstep, Curve::Quintic}) {
        for (const int step : {2, 7, 64, 160}) {
            CAPTURE(step);
            const auto ease = make_ease(step, curve);
            CHECK(ease.front() == 0);
            CHECK(std::is_sorted(ease.begin(), ease.end()));
            CHECK(ease.back() <= 256);
        }
    }
    // Halfway, every symmetric curve is exactly half.
    CHECK(make_ease(64, Curve::Linear)[32] == 128);
    CHECK(make_ease(64, Curve::Smoothstep)[32] == 128);
    CHECK(make_ease(64, Curve::Quintic)[32] == 128);
    // Near the start the smoother curves move slower.
    CHECK(make_ease(64, Curve::Quintic)[8] < make_ease(64, Curve::Smoothstep)[8]);
    CHECK(make_ease(64, Curve::Smoothstep)[8] < make_ease(64, Curve::Linear)[8]);
}

TEST_CASE("shapes fold the noise and stay in range") {
    CHECK(shape_value(0, Shape::Normal) == 0);
    CHECK(shape_value(200, Shape::Normal) == 200);
    CHECK(shape_value(0, Shape::Ridged) == 0);
    CHECK(shape_value(255, Shape::Ridged) == 0);
    CHECK(shape_value(127, Shape::Ridged) == 254); // a ridge where the noise crosses its middle
    CHECK(shape_value(0, Shape::Billowed) == 255);
    CHECK(shape_value(128, Shape::Billowed) == 1);
    for (int v = 0; v <= 255; ++v) {
        CHECK(shape_value(v, Shape::Ridged) + shape_value(v, Shape::Billowed) == 255);
    }
}

TEST_CASE("every layer option is deterministic and in range") {
    for (const Curve curve : {Curve::Linear, Curve::Smoothstep, Curve::Quintic}) {
        for (const Shape shape : {Shape::Normal, Shape::Ridged, Shape::Billowed}) {
            for (const int warp : {0, 12}) {
                NoiseSpec n{40, 4, 60, 40, curve, shape, warp, 32};
                Rng a(5, 1);
                Rng b(5, 1);
                Grid<int> ga(kW, kH, 0);
                Grid<int> gb(kW, kH, 0);
                const int wa = fill_noise(ga, a, n);
                fill_noise(gb, b, n);
                CHECK(ga == gb);
                Grid<int> norm = ga;
                for (int y = 0; y < kH; ++y) {
                    for (int x = 0; x < kW; ++x) {
                        norm.at(x, y) /= wa;
                    }
                }
                CHECK(in_byte_range(norm));
            }
        }
    }
}

TEST_CASE("domain warp off changes nothing; on, it moves the noise") {
    Rng a(3, 1);
    Rng b(3, 1);
    Rng c(3, 1);
    Grid<int> plain(kW, kH, 0);
    Grid<int> zero(kW, kH, 0);
    Grid<int> warped(kW, kH, 0);
    fill_noise(plain, a, NoiseSpec{48});
    NoiseSpec z{48};
    z.warp_step = 20; // ignored while warp_cells is 0
    fill_noise(zero, b, z);
    CHECK(plain == zero);
    CHECK(a.next_u32() == b.next_u32()); // no extra draws without warp
    NoiseSpec w{48};
    w.warp_cells = 16;
    fill_noise(warped, c, w);
    CHECK_FALSE(plain == warped);
}

TEST_CASE("octave rules: lacunarity shrinks the steps, persistence the weights") {
    Rng r(1, 1);
    Grid<int> g(kW, kH, 0);
    std::array<Grid<int>, 3> oct{{{kW, kH, 0}, {kW, kH, 0}, {kW, kH, 0}}};
    std::array<int, 3> weights{};
    const int total = fill_noise(g, r, NoiseSpec{64, 3, 50, 25}, &oct, &weights);
    CHECK(weights == std::array<int, 3>{4096, 1024, 256});
    CHECK(total == 4096 + 1024 + 256);
    Rng r1(1, 1);
    Grid<int> g1(kW, kH, 0);
    std::array<int, 3> w1{};
    fill_noise(g1, r1, NoiseSpec{64, 1}, nullptr, &w1);
    CHECK(w1 == std::array<int, 3>{4096, 0, 0}); // octaves past the count weigh nothing
}

TEST_CASE("validate: defaults are already valid; wild values are clamped") {
    GenParams p;
    CHECK(validate(p) == 0);
    CHECK(p == GenParams{});
    GenParams w;
    w.forest_noise.octaves = 99;
    w.forest_noise.step = 0;
    w.lakes_max = -3;
    w.core_area = Rect{-10, 500, 2, 2};
    w.forest_noise.curve = static_cast<Curve>(9);
    w.forest_noise.basis = static_cast<Basis>(7);
    CHECK(validate(w) > 0);
    CHECK(w.forest_noise.octaves == kMaxOctaves);
    CHECK(w.forest_noise.step == 2);
    CHECK(w.lakes_max >= w.lakes_min);
    CHECK(w.core_area.x >= 0);
    CHECK(w.core_area.x + w.core_area.w <= kMapW);
    CHECK(w.core_area.y + w.core_area.h <= kMapH);
    CHECK(w.forest_noise.curve == Curve::Smoothstep);
    CHECK(w.forest_noise.basis == Basis::Value); // NoiseSpec's default
    CHECK(validate(w) == 0);                     // stable
}

TEST_CASE("fuzz: random valid settings never break the generator") {
    Rng r(0xF00Dull, 3);
    const auto pick = [&](int lo, int hi) { return rand_range(r, lo, hi); };
    int fallbacks = 0;
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        CAPTURE(seed);
        GenParams p;
        for (NoiseSpec* n :
             {&p.fertility_noise, &p.moisture_noise, &p.mountain_noise, &p.outcrop_noise,
              &p.cave_noise, &p.river_noise, &p.lake_rough, &p.forest_noise}) {
            n->step = pick(2, 160);
            n->octaves = pick(1, kMaxOctaves);
            n->lacunarity_pct = pick(10, 100);
            n->persistence_pct = pick(0, 100);
            n->curve = static_cast<Curve>(pick(0, kCurves - 1));
            n->shape = static_cast<Shape>(pick(0, kShapes - 1));
            n->warp_cells = pick(0, 1) == 0 ? 0 : pick(1, 64);
            n->basis = static_cast<Basis>(pick(0, kBases - 1));
        }
        p.mountain_share_pct = pick(0, 60);
        p.mountain_edge_bias = pick(0, 255);
        p.cave_share_pct = pick(0, 100);
        p.cave_min_cells = pick(1, 200);
        p.river_pct = pick(0, 100);
        p.river_deep_width = pick(0, 12);
        p.fords_min = pick(1, 6);
        p.fords_max = pick(1, 8);
        p.lakes_min = pick(0, 10);
        p.lakes_max = pick(0, 20);
        p.lake_radius_max = pick(2, 60);
        p.tree_rich_pct = pick(0, 100);
        p.attempts = 4; // keep the sweep quick: hard settings fall back sooner
        validate(p);
        GenLayers L;
        const Map m = generate_map(seed, p, &L);
        REQUIRE(m.core.has_value());
        CHECK(m.spawns.size() == p.spawn_waves.size());
        if (m.stats.fallback) {
            ++fallbacks;
            CHECK_FALSE(L.valid);
        } else {
            CHECK(L.valid);
            CHECK(placement_ok(m, *m.core, kCoreSub));
        }
    }
    MESSAGE("fuzz fallbacks: ", fallbacks, " / 20");
}
