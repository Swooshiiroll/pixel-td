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

TEST_CASE("range grows with height above the target and shrinks uphill") {
    const GenParams p; // +5% per 10 units, no cap downhill, at worst -25% uphill
    const int base = 72;
    CHECK(effective_range(base, 100, 100, p) == 72);
    CHECK(effective_range(base, 120, 100, p) == 72 * 110 / 100);
    CHECK(effective_range(base, 200, 100, p) == 72 * 150 / 100); // 100 above: +50%
    CHECK(effective_range(base, 200, 0, p) == 72 * 200 / 100);   // 200 above: +100%, no cap
    CHECK(max_reach(base, 150, p) == effective_range(base, 150, 0, p));
    CHECK(effective_range(base, 100, 120, p) == 72 * 90 / 100); // uphill
    CHECK(effective_range(base, 0, 200, p) == 72 * 75 / 100);   // floored
}

TEST_CASE("ground height under a point") {
    Range r;
    r.map.height.at(3, 4) = 77;
    CHECK(ground_at(r.map, centre_of(Cell{3, 4})) == 77);
    CHECK(ground_at(r.map, PointPx{-50, -50}) == r.map.height.at(0, 0));
}
