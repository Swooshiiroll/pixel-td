#include "core/erosion.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include <doctest/doctest.h>

using namespace pixeltd;

namespace {

// A cone: highest in the middle, falling 2 height units per cell (in 1/65536 units).
Grid<int> cone(int side) {
    Grid<int> g(side, side, 0);
    const int c = side / 2;
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int d = std::abs(x - c) + std::abs(y - c);
            g.at(x, y) = static_cast<int>(std::max(0, 160 - 2 * d) * kErosionOne);
        }
    }
    return g;
}

long long total(const Grid<int>& g) {
    long long t = 0;
    for (const int v : g.cells()) {
        t += v;
    }
    return t;
}

GenParams erosion_params(int droplets) {
    GenParams p;
    p.erosion_droplets = droplets;
    return p;
}

} // namespace

TEST_CASE("erosion is deterministic") {
    Grid<int> a = cone(64);
    Grid<int> b = cone(64);
    Rng ra(11, 3);
    Rng rb(11, 3);
    const GenParams p = erosion_params(3000);
    CHECK(erode(a, ra, p) == erode(b, rb, p));
    CHECK(a == b);
}

TEST_CASE("erosion moves sediment and loses only what leaves") {
    Grid<int> g = cone(64);
    const Grid<int> before = g;
    Rng rng(4, 1);
    const long long lost = erode(g, rng, erosion_params(3000));
    CHECK(lost >= 0);
    CHECK(total(before) - total(g) == lost);
    int cut = 0;    // cells worn lower
    int filled = 0; // cells raised by sediment
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            cut += g.at(x, y) < before.at(x, y) ? 1 : 0;
            filled += g.at(x, y) > before.at(x, y) ? 1 : 0;
            REQUIRE(g.at(x, y) >= 0);
        }
    }
    CHECK(cut > 100);
    CHECK(filled > 0); // a steady slope mostly erodes; sediment settles on the flat foot
}

TEST_CASE("erosion carves channels into a smooth slope") {
    // Across a slope, a cone ring is the same height all round; after erosion some cells on it
    // are lower than both neighbours along the ring: gullies running downhill.
    Grid<int> g = cone(96);
    Rng rng(8, 2);
    erode(g, rng, erosion_params(20000));
    int gullies = 0;
    const int c = 48;
    for (int x = c + 5; x < c + 30; ++x) { // along a row 10 cells above the top
        const int here = g.at(x, c - 10);
        if (here < g.at(x - 1, c - 10) && here < g.at(x + 1, c - 10)) {
            ++gullies;
        }
    }
    CHECK(gullies > 0);
}

TEST_CASE("no droplets, no change") {
    Grid<int> g = cone(32);
    const Grid<int> before = g;
    Rng rng(1, 1);
    CHECK(erode(g, rng, erosion_params(0)) == 0);
    CHECK(g == before);
}
