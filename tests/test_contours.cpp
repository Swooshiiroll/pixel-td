#include "core/contours.hpp"

#include <cmath>
#include <map>
#include <utility>

#include <doctest/doctest.h>

using namespace pixeltd;

namespace {

float centre(int c) {
    return static_cast<float>(c * kCellPx + kCellPx / 2);
}

} // namespace

TEST_CASE("flat ground has no contour lines") {
    const Grid<std::uint8_t> flat(40, 20, 100);
    CHECK(build_contours(flat).total() == 0);
}

TEST_CASE("a steady slope gives evenly spaced straight lines every 10 units") {
    // Height rises by 4 per cell to the east: levels 10, 20, ..., 156 / 10 = 15 lines, each
    // crossing all 9 rows of squares.
    Grid<std::uint8_t> slope(40, 10, 0);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 40; ++x) {
            slope.at(x, y) = static_cast<std::uint8_t>(4 * x);
        }
    }
    const Contours c = build_contours(slope, 10, 50, 8);
    CHECK(c.total() == 15 * 9);
    int majors = 0;
    bool straight = true;
    bool placed = true;
    for (const auto& chunk : c.chunks) {
        for (const ContourSegment& s : chunk) {
            straight = straight && s.x0 == s.x1; // north-south lines
            // Height 4 * x between centres: level L sits L / 4 cells east of cell 0's centre.
            const float level = (s.x0 - centre(0)) / static_cast<float>(kCellPx) * 4.0f;
            const float nearest = std::round(level / 10.0f) * 10.0f;
            placed = placed && std::abs(level - nearest) < 0.01f;
            majors += s.major ? 1 : 0;
        }
    }
    CHECK(straight);
    CHECK(placed);
    CHECK(majors == 3 * 9); // levels 50, 100, 150
}

TEST_CASE("a single peak gives one closed loop per level") {
    Grid<std::uint8_t> g(9, 9, 0);
    g.at(4, 4) = 100;
    const Contours c = build_contours(g, 10, 50, 4);
    CHECK(c.total() == 9 * 4); // levels 10..90 (100 only touches the peak), each a diamond of 4
    // Every endpoint is shared by exactly two segments of the same level: the loops are closed.
    std::map<std::pair<long, long>, int> ends;
    for (const auto& chunk : c.chunks) {
        for (const ContourSegment& s : chunk) {
            ++ends[{std::lround(s.x0 * 100.0f), std::lround(s.y0 * 100.0f)}];
            ++ends[{std::lround(s.x1 * 100.0f), std::lround(s.y1 * 100.0f)}];
        }
    }
    bool closed = true;
    for (const auto& [point, count] : ends) {
        closed = closed && count == 2;
    }
    CHECK(closed);
}

TEST_CASE("every segment is in the chunk of the square it crosses") {
    Grid<std::uint8_t> g(70, 45, 0);
    for (int y = 0; y < 45; ++y) {
        for (int x = 0; x < 70; ++x) {
            g.at(x, y) = static_cast<std::uint8_t>((x * 7 + y * 5) % 200);
        }
    }
    const Contours c = build_contours(g, 10, 50, 32);
    CHECK(c.chunks_w == 3);
    CHECK(c.chunks_h == 2);
    bool ok = true;
    std::size_t counted = 0;
    for (int cy = 0; cy < c.chunks_h; ++cy) {
        for (int cx = 0; cx < c.chunks_w; ++cx) {
            for (const ContourSegment& s : c.chunk(cx, cy)) {
                ++counted;
                // The segment's midpoint lies inside its square, whose top-left cell is in this
                // chunk.
                const float mx = (s.x0 + s.x1) / 2.0f;
                const float my = (s.y0 + s.y1) / 2.0f;
                // A segment can run along a square's right or bottom edge, so a midpoint exactly on
                // a chunk border may belong to the chunk before it.
                const float u = (mx - centre(0)) / static_cast<float>(kCellPx);
                const float v = (my - centre(0)) / static_cast<float>(kCellPx);
                const auto in = [](float t, int chunk) {
                    const int sq = static_cast<int>(t);
                    return sq / 32 == chunk ||
                           (t == static_cast<float>(sq) && sq > 0 && (sq - 1) / 32 == chunk);
                };
                ok = ok && in(u, cx) && in(v, cy);
            }
        }
    }
    CHECK(ok);
    CHECK(counted == c.total());
    CHECK(counted > 0);
}
