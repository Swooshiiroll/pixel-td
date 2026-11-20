#include "core/simplex.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <doctest/doctest.h>

using namespace pixeltd;

TEST_CASE("simplex noise is deterministic, in range and centred") {
    long long sum = 0;
    int lo = 255;
    int hi = 0;
    bool same = true;
    constexpr int kSide = 256;
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const int v = simplex_cell(77, x, y, 16);
            same = same && v == simplex_cell(77, x, y, 16);
            sum += v;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    CHECK(same);
    CHECK(lo >= 0);
    CHECK(hi <= 255);
    CHECK(hi - lo > 150); // it uses most of the range
    const long long mean = sum / (kSide * kSide);
    CHECK(mean > 118);
    CHECK(mean < 138);
}

TEST_CASE("simplex noise differs by seed") {
    int differ = 0;
    for (int x = 0; x < 200; ++x) {
        differ += simplex_cell(1, x, 7, 8) != simplex_cell(2, x, 7, 8) ? 1 : 0;
    }
    CHECK(differ > 150);
}

TEST_CASE("simplex noise has no square look: diagonal and straight changes match") {
    // For noise with no preferred direction, the squared change over a diagonal step (length
    // sqrt 2) is about twice that over a straight step. Value noise, blended on a square grid,
    // doesn't keep this, which is the squarish look this replaces (#29).
    long long straight = 0;
    long long diagonal = 0;
    for (int y = 0; y < 400; ++y) {
        for (int x = 0; x < 400; ++x) {
            const int v = simplex_cell(5, x, y, 24);
            const int dx = simplex_cell(5, x + 1, y, 24) - v;
            const int dy = simplex_cell(5, x, y + 1, 24) - v;
            const int dd = simplex_cell(5, x + 1, y + 1, 24) - v;
            straight += dx * dx + dy * dy;
            diagonal += 2 * dd * dd;
        }
    }
    // With no preferred direction, dd^2 averages dx^2 + dy^2, so 2 * dd^2 summed over
    // (dx^2 + dy^2) summed is about 2.
    const long long ratio_x100 = diagonal * 100 / straight;
    CAPTURE(ratio_x100);
    CHECK(ratio_x100 > 180);
    CHECK(ratio_x100 < 220);
}

TEST_CASE("simplex noise is continuous: neighbouring cells differ little") {
    int worst = 0;
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            const int v = simplex_cell(9, x, y, 32);
            worst = std::max(worst, std::abs(simplex_cell(9, x + 1, y, 32) - v));
            worst = std::max(worst, std::abs(simplex_cell(9, x, y + 1, 32) - v));
        }
    }
    CHECK(worst <= 24); // features 32 cells across: no jumps
}

// Manual look at the noise: pixeltd_tests -tc="*simplex ascii*" --no-skip
TEST_CASE("simplex ascii (manual)" * doctest::skip()) {
    for (int y = 0; y < 48; y += 2) {
        std::string row;
        for (int x = 0; x < 120; ++x) {
            const int v = simplex_cell(12345, x, y, 16);
            row += " .:-=+*#%@"[v * 10 / 256];
        }
        MESSAGE(row);
    }
}

// Manual: directional balance at a large scale. pixeltd_tests -tc="*simplex balance*" --no-skip
TEST_CASE("simplex balance (manual)" * doctest::skip()) {
    for (const int step : {24, 160}) {
        long long ex = 0, ey = 0, ed = 0, ea = 0; // x, y, diagonal (1,1), anti-diagonal (1,-1)
        const int lag = step / 4;
        for (int y = lag; y < 900; y += 3) {
            for (int x = 0; x < 900; x += 3) {
                const int v = simplex_cell(5, x, y, step);
                const int a = simplex_cell(5, x + lag, y, step) - v;
                const int b = simplex_cell(5, x, y + lag, step) - v;
                const int c = simplex_cell(5, x + lag, y + lag, step) - v;
                const int d = simplex_cell(5, x + lag, y - lag, step) - v;
                ex += a * a;
                ey += b * b;
                ed += c * c;
                ea += d * d;
            }
        }
        MESSAGE("step ", step, "  x ", ex, "  y ", ey, "  diag ", ed, "  anti ", ea);
    }
}
