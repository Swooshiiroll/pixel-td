#include "core/los.hpp"

#include <doctest/doctest.h>

#include "core/mapgen.hpp"

using namespace pixeltd;

namespace {

// Open ground (the fallback map has no formations), a tower at cell (10, 10) and a target 10 cells
// to its right.
struct Range {
    Map map = make_fallback_map(1);
    PointPx from = centre_of(Cell{10, 10});
    PointPx to = centre_of(Cell{20, 10});
};

} // namespace

TEST_CASE("open ground never blocks") {
    const Range r;
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Direct));
    CHECK(clear_shot(r.map, r.from, centre_of(Cell{25, 30}), Shot::Direct));
    CHECK(clear_shot(r.map, r.from, r.from, Shot::Direct));
    CHECK(can_see(r.map, r.from, r.to));
}

TEST_CASE("rock formations block sight and direct fire; lobbed shots arc over them") {
    Range r;
    r.map.formation.at(15, 10) = 200;
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Shot::Direct));
    CHECK_FALSE(can_see(r.map, r.from, r.to));
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Lobbed));
    // However short the rock (its value is looks only).
    r.map.formation.at(15, 10) = 1;
    CHECK_FALSE(can_see(r.map, r.from, r.to));
}

TEST_CASE("a formation beside the line doesn't block") {
    Range r;
    r.map.formation.at(15, 11) = 1;
    r.map.formation.at(15, 9) = 1;
    CHECK(clear_shot(r.map, r.from, r.to, Shot::Direct));
}

TEST_CASE("vertical and backwards lines") {
    Range r;
    const PointPx below = centre_of(Cell{10, 20});
    r.map.formation.at(10, 15) = 1;
    CHECK_FALSE(can_see(r.map, r.from, below));
    CHECK_FALSE(can_see(r.map, below, r.from));
    r.map.formation.at(15, 10) = 1;
    CHECK_FALSE(can_see(r.map, r.to, r.from));
}

TEST_CASE("a line exactly through a cell corner counts both cells beside it") {
    Range r;
    const PointPx diag = centre_of(Cell{12, 12});
    CHECK(can_see(r.map, r.from, diag));
    r.map.formation.at(11, 10) = 1;
    CHECK_FALSE(can_see(r.map, r.from, diag));
    r.map.formation.at(11, 10) = 0;
    r.map.formation.at(10, 11) = 1;
    CHECK_FALSE(can_see(r.map, r.from, diag));
    r.map.formation.at(10, 11) = 0;
    r.map.formation.at(12, 10) = 1; // not beside the line
    CHECK(can_see(r.map, r.from, diag));
}

TEST_CASE("the shooter's and the target's own cells never block") {
    Range r;
    r.map.formation.at(20, 10) = 1; // the target stands against a rock, in its cell
    r.map.formation.at(10, 10) = 1;
    CHECK(can_see(r.map, r.from, r.to));
    // A light tower's centre is the middle of its 3x3 subcells.
    const PointPx tower = centre_of(SubCell{20, 20}, kLightSub);
    CHECK(tower == PointPx{20 * kSubPx + 12, 20 * kSubPx + 12});
}

TEST_CASE("the reach is a plain circle, the same everywhere") {
    CHECK(in_reach(72, 72, 0));
    CHECK_FALSE(in_reach(72, 73, 0));
    CHECK(in_reach(72, 0, -72));
    CHECK(in_reach(72, 50, 50)); // 70.7 px
    CHECK_FALSE(in_reach(72, 51, 51));
    CHECK(in_reach(72, 0, 0));
    CHECK_FALSE(in_reach(0, 1, 0));
}
