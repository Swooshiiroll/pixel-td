#include "core/los.hpp"

#include <doctest/doctest.h>

#include "core/mapgen.hpp"

using namespace pixeltd;

namespace {

// Flat ground, a shooter at cell (10, 10) and a target 10 cells to its right.
struct Range {
    Map map = make_fallback_map(1);
    PointPx from = centre_of(Cell{10, 10});
    PointPx to = centre_of(Cell{20, 10});
};

} // namespace

TEST_CASE("open ground never blocks") {
    const Range r;
    CHECK(clear_shot(r.map, r.from, r.to, Height::Low, Shot::Direct));
    CHECK(clear_shot(r.map, r.from, centre_of(Cell{25, 30}), Height::Low, Shot::Direct));
    CHECK(clear_shot(r.map, r.from, r.from, Height::Low, Shot::Direct));
}

TEST_CASE("rocks block direct fire from low ground only") {
    Range r;
    r.map.terrain.at(15, 10) = Terrain::Rock;
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Height::Low, Shot::Direct));
    CHECK(clear_shot(r.map, r.from, r.to, Height::High, Shot::Direct)); // fires over
    CHECK(clear_shot(r.map, r.from, r.to, Height::Low, Shot::Lobbed));  // arcs over
    CHECK(clear_shot(r.map, r.from, r.to, Height::High, Shot::Lobbed));
    // Sight is always direct: a lobbed tower on low ground still can't see past the rock.
    CHECK_FALSE(can_see(r.map, r.from, r.to, Height::Low));
    CHECK(can_see(r.map, r.from, r.to, Height::High));
}

TEST_CASE("cliffs block every shot") {
    Range r;
    r.map.terrain.at(15, 10) = Terrain::Cliff;
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Height::Low, Shot::Direct));
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Height::High, Shot::Direct));
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Height::Low, Shot::Lobbed));
    CHECK_FALSE(clear_shot(r.map, r.from, r.to, Height::High, Shot::Lobbed));
}

TEST_CASE("high ground doesn't block") {
    Range r;
    r.map.terrain.at(15, 10) = Terrain::High;
    CHECK(clear_shot(r.map, r.from, r.to, Height::Low, Shot::Direct));
}

TEST_CASE("a blocker beside the line doesn't block") {
    Range r;
    r.map.terrain.at(15, 11) = Terrain::Cliff;
    r.map.terrain.at(15, 9) = Terrain::Cliff;
    CHECK(clear_shot(r.map, r.from, r.to, Height::Low, Shot::Direct));
}

TEST_CASE("vertical and backwards lines") {
    Range r;
    const PointPx below = centre_of(Cell{10, 20});
    r.map.terrain.at(10, 15) = Terrain::Rock;
    CHECK_FALSE(clear_shot(r.map, r.from, below, Height::Low, Shot::Direct));
    CHECK_FALSE(clear_shot(r.map, below, r.from, Height::Low, Shot::Direct));
    r.map.terrain.at(15, 10) = Terrain::Rock;
    CHECK_FALSE(clear_shot(r.map, r.to, r.from, Height::Low, Shot::Direct));
}

TEST_CASE("a line exactly through a cell corner counts both cells beside it") {
    // From (10,10)'s centre to (12,12)'s centre: the 45-degree line passes exactly through the
    // corners between cells, so the cells beside the diagonal count as crossed.
    Range r;
    const PointPx diag = centre_of(Cell{12, 12});
    CHECK(clear_shot(r.map, r.from, diag, Height::Low, Shot::Direct));
    r.map.terrain.at(11, 10) = Terrain::Rock;
    CHECK_FALSE(clear_shot(r.map, r.from, diag, Height::Low, Shot::Direct));
    r.map.terrain.at(11, 10) = Terrain::Ground;
    r.map.terrain.at(10, 11) = Terrain::Cliff;
    CHECK_FALSE(clear_shot(r.map, r.from, diag, Height::High, Shot::Direct));
    r.map.terrain.at(10, 11) = Terrain::Ground;
    r.map.terrain.at(12, 10) = Terrain::Cliff; // not beside the line
    CHECK(clear_shot(r.map, r.from, diag, Height::Low, Shot::Direct));
}

TEST_CASE("a shooter's own footprint never blocks") {
    // A light tower's centre sits on the corner of its 2x2 footprint; those cells are buildable
    // ground, never blockers, so they can't block its own shots.
    Range r;
    const PointPx tower = centre_of(Cell{10, 10}, kLightSize);
    CHECK(clear_shot(r.map, tower, r.to, Height::Low, Shot::Direct));
    CHECK(clear_shot(r.map, tower, centre_of(Cell{0, 0}), Height::Low, Shot::Direct));
}

TEST_CASE("height comes from the ground under the tower") {
    Range r;
    r.map.terrain.at(3, 3) = Terrain::High;
    CHECK(height_at(r.map, {3, 3}) == Height::High);
    CHECK(height_at(r.map, {4, 3}) == Height::Low);
    CHECK(height_at(r.map, {-1, 3}) == Height::Low);
}
