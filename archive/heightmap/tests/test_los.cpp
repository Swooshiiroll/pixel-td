#include "core/los.hpp"

#include <doctest/doctest.h>

#include "core/mapgen.hpp"

using namespace pixeltd;

namespace {

void fill(Map& m, Rect r, int height) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            m.height.at(x, y) = static_cast<std::uint8_t>(height);
        }
    }
}

// Flat ground at height 100, a tower at cell (10, 10) and a target 10 cells to its right.
struct Range {
    GenParams p;
    Map map = make_fallback_map(1, p);
    PointPx from = centre_of(Cell{10, 10});
    PointPx to = centre_of(Cell{20, 10});
};

} // namespace

TEST_CASE("open ground never blocks") {
    const Range r;
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Direct, r.p));
    CHECK(clear_shot(r.map, r.from, centre_of(Cell{25, 30}), Shot::Direct, r.p));
    CHECK(clear_shot(r.map, r.from, r.from, Shot::Direct, r.p));
}

TEST_CASE("a ridge blocks direct fire and sight from low ground, not from high") {
    Range r;
    fill(r.map, {15, 0, 1, 40}, 120); // a ridge across the line
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Shot::Direct, r.p));
    CHECK_FALSE(can_see(r.map, r.from, r.to, r.p));
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Lobbed, r.p)); // lobbed shots arc over terrain
    // Raise the tower's ground: now its eye line clears the ridge.
    fill(r.map, {9, 9, 3, 3}, 150);
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Direct, r.p));
    CHECK(can_see(r.map, r.from, r.to, r.p));
}

TEST_CASE("a low rise under the eye line doesn't block") {
    Range r;
    fill(r.map, {15, 0, 1, 40}, 105); // eye 110, target 104: the line passes above 105
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Direct, r.p));
}

TEST_CASE("rock formations block every shot and sight") {
    Range r;
    r.map.formation.at(15, 10) = 1;
    r.map.height.at(15, 10) = 240;
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Shot::Direct, r.p));
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Shot::Lobbed, r.p));
    CHECK_FALSE(can_see(r.map, r.from, r.to, r.p));
    // Even from high ground.
    fill(r.map, {9, 9, 3, 3}, 200);
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Shot::Direct, r.p));
}

TEST_CASE("a formation beside the line doesn't block") {
    Range r;
    r.map.formation.at(15, 11) = 1;
    r.map.formation.at(15, 9) = 1;
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Lobbed, r.p));
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Direct, r.p));
}

TEST_CASE("vertical and backwards lines") {
    Range r;
    const PointPx below = centre_of(Cell{10, 20});
    r.map.formation.at(10, 15) = 1;
    CHECK_FALSE(clear_shot(r.map, r.from, below, Shot::Lobbed, r.p));
    CHECK_FALSE(clear_shot(r.map, below, r.from, Shot::Lobbed, r.p));
    r.map.formation.at(15, 10) = 1;
    CHECK_FALSE(clear_shot(r.map, r.to, r.from, Shot::Lobbed, r.p));
}

TEST_CASE("a line exactly through a cell corner counts both cells beside it") {
    Range r;
    const PointPx diag = centre_of(Cell{12, 12});
    CHECK(clear_shot(r.map, r.from, diag, Shot::Direct, r.p));
    r.map.formation.at(11, 10) = 1;
    CHECK_FALSE(clear_shot(r.map, r.from, diag, Shot::Lobbed, r.p));
    r.map.formation.at(11, 10) = 0;
    r.map.height.at(10, 11) = 200;
    CHECK_FALSE(clear_shot(r.map, r.from, diag, Shot::Direct, r.p));
    r.map.height.at(10, 11) = 100;
    r.map.formation.at(12, 10) = 1; // not beside the line
    CHECK(clear_shot(r.map, r.from, diag, Shot::Lobbed, r.p));
}

TEST_CASE("the shooter's and the target's own cells never block") {
    Range r;
    r.map.height.at(20, 10) = 180; // the target stands on a peak
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Direct, r.p));
    // A light tower's centre is the middle of its 3x3 subcells.
    const PointPx tower = centre_of(SubCell{20, 20}, kLightSub);
    CHECK(tower == PointPx{20 * kSubPx + 12, 20 * kSubPx + 12});
    CHECK(clear_shot(r.map, tower, centre_of(Cell{0, 0}), Shot::Direct, r.p));
}

TEST_CASE("range follows the grade between tower and target, not the altitude") {
    const GenParams p; // 10% per unit of fall per cell, at most +-50%
    const int base = 72;
    const int four_cells = 4 * kCellPx;
    // Flat ground: the base range at any altitude.
    CHECK(effective_range(base, 50, 50, four_cells, p) == 72);
    CHECK(effective_range(base, 180, 180, four_cells, p) == 72);
    // Downhill: a fall of 10 over 4 cells (grade 2.5) is +25%; 20 over 4 cells (grade 5) is +50%.
    CHECK(effective_range(base, 110, 100, four_cells, p) == 72 * 125 / 100);
    CHECK(effective_range(base, 120, 100, four_cells, p) == 72 * 150 / 100);
    // Uphill: the same, shorter.
    CHECK(effective_range(base, 100, 110, four_cells, p) == 72 * 75 / 100);
    CHECK(effective_range(base, 100, 120, four_cells, p) == 72 * 50 / 100);
    // Both ways the change is capped at 50%, however steep.
    CHECK(effective_range(base, 200, 0, four_cells, p) == 72 * 150 / 100);
    CHECK(effective_range(base, 0, 200, four_cells, p) == 72 * 50 / 100);
    // The same fall over twice the distance is half the grade (12.5% rounds towards zero: 12%).
    CHECK(effective_range(base, 110, 100, 8 * kCellPx, p) == 72 * 112 / 100);
    // Under one cell counts as one cell.
    CHECK(effective_range(base, 102, 100, 3, p) == effective_range(base, 102, 100, kCellPx, p));
    CHECK(max_reach(base, p) == 72 * 150 / 100);
}

TEST_CASE("in reach: ovals on slopes, circles on flat ground at any height") {
    const GenParams p;
    const int base = 72;
    // Flat at two altitudes: exactly the base range, a circle.
    for (const int h : {40, 190}) {
        CHECK(in_reach(base, h, h, 72, 0, p));
        CHECK_FALSE(in_reach(base, h, h, 73, 0, p));
        CHECK(in_reach(base, h, h, 50, 50, p)); // 70.7 px
        CHECK_FALSE(in_reach(base, h, h, 51, 51, p));
    }
    // A slope falling 2 units per cell eastwards: east (downhill) reaches further than west.
    const auto ground_at_dx = [](long long dx) { return static_cast<int>(100 - 2 * dx / kCellPx); };
    // 80 px east falls 10 (grade 2): +20%, 86 px, so it's in reach though past the 72 px base;
    // 80 px west climbs 10: -20%, 57 px, out of reach.
    const long long east = 80;
    CHECK(in_reach(base, 100, ground_at_dx(east), east, 0, p));
    CHECK_FALSE(in_reach(base, 100, ground_at_dx(-east), -east, 0, p));
    CHECK(in_reach(base, 100, ground_at_dx(-57), -57, 0, p)); // -20% leaves 57
}

TEST_CASE("integer square root") {
    CHECK(isqrt(0) == 0);
    CHECK(isqrt(-5) == 0);
    CHECK(isqrt(1) == 1);
    CHECK(isqrt(2) == 1);
    CHECK(isqrt(15) == 3);
    CHECK(isqrt(16) == 4);
    CHECK(isqrt(17) == 4);
    CHECK(isqrt(1000000) == 1000);
    CHECK(isqrt(999999) == 999);
    CHECK(isqrt(3000000000LL) == 54772);
}

TEST_CASE("ground height under a point") {
    Range r;
    r.map.height.at(3, 4) = 77;
    CHECK(ground_at(r.map, centre_of(Cell{3, 4})) == 77);
    CHECK(ground_at(r.map, PointPx{-50, -50}) == r.map.height.at(0, 0));
}
