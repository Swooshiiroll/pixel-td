#include "core/flow.hpp"

#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

using namespace pixeltd;

namespace {

// Synthetic route inputs: every node at full speed, no blocks, unless a test paints them.
struct Field {
    Grid<std::uint8_t> speed{kNodeW, kNodeH, 100};
    Grid<std::uint16_t> blocks{kSubW, kSubH, 0};
    std::vector<std::int32_t> cost;
    FlowField field;

    [[nodiscard]] FlowInput in(bool breach = false) const {
        return FlowInput{&speed, &blocks, &cost, breach};
    }
    void build(std::vector<SubCell> targets, bool breach = false) {
        field.build(in(breach), targets);
    }
    // A wall of block `id` over subcell rows [y0, y1), every column but [gap_x, gap_x + gap_w).
    void wall(int y0, int y1, std::uint16_t id, int gap_x = -10, int gap_w = 0, int x0 = 0,
              int x1 = kSubW) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (x < gap_x || x >= gap_x + gap_w) {
                    blocks.at(x, y) = id;
                }
            }
        }
    }
    // Follows the field from `from` to a target; the nodes walked (not counting the start).
    [[nodiscard]] std::vector<SubCell> walk(SubCell from, bool breach = false) const {
        std::vector<SubCell> path;
        SubCell n = from;
        for (int i = 0; i < 20000; ++i) {
            const auto nx = field.next(in(breach), n);
            if (!nx) {
                break;
            }
            n = *nx;
            path.push_back(n);
        }
        return path;
    }
};

} // namespace

TEST_CASE("open ground: routes cost straight and diagonal steps") {
    Field f;
    f.build({{100, 100}});
    CHECK(f.field.dist({100, 100}) == 0);
    CHECK(f.field.dist({110, 100}) == 10 * kStraightCost);
    CHECK(f.field.dist({100, 90}) == 10 * kStraightCost);
    CHECK(f.field.dist({110, 110}) == 10 * kDiagonalCost);
    CHECK(f.field.dist({120, 105}) == 5 * kDiagonalCost + 15 * kStraightCost);
    // Following the field gets to the target in as many steps as the longer axis.
    const auto path = f.walk({120, 105});
    REQUIRE_FALSE(path.empty());
    CHECK(path.back() == SubCell{100, 100});
    CHECK(path.size() == 20);
}

TEST_CASE("a 16 px gap lets walkers through wherever it sits; an 8 px gap doesn't") {
    for (const int gap_x : {700, 701}) { // both alignments on the subgrid
        for (const int gap_w : {1, 2, 3}) {
            CAPTURE(gap_x);
            CAPTURE(gap_w);
            Field f;
            f.cost = {1};
            f.wall(200, 203, 1, gap_x, gap_w);
            f.build({{700, 100}});
            const bool through = f.field.reached({700, 300});
            CHECK(through == (gap_w >= 2));
        }
    }
}

TEST_CASE("no squeezing diagonally past a corner") {
    // Nodes a and b are diagonal neighbours, both free; the two nodes beside the corner between
    // them are each blocked by a single subcell.
    Field f;
    f.cost = {1};
    const SubCell a{10, 10};
    const SubCell b{11, 11};
    f.build({b});
    CHECK(f.field.dist(a) == kDiagonalCost);
    f.blocks.at(12, 10) = 1; // blocks node (11, 10)
    f.blocks.at(10, 12) = 1; // blocks node (10, 11)
    REQUIRE(node_passable(f.in(), a));
    REQUIRE(node_passable(f.in(), b));
    f.build({b});
    CHECK(f.field.dist(a) > kDiagonalCost);
}

TEST_CASE("fastest route: around slow ground when that's quicker, across when it isn't") {
    const auto slow_band = [](Field& f, int x0, int x1) {
        for (int y = 190; y <= 210; ++y) {
            for (int x = x0; x <= x1; ++x) {
                f.speed.at(x, y) = 50;
            }
        }
    };
    const auto crosses = [](const Field& f, const std::vector<SubCell>& path) {
        for (const SubCell n : path) {
            if (f.speed.at(n.x, n.y) < 100) {
                return true;
            }
        }
        return false;
    };
    {
        // A short band: the detour round its end is cheaper than wading through.
        Field f;
        slow_band(f, 490, 510);
        f.build({{500, 100}});
        const auto path = f.walk({500, 300});
        REQUIRE_FALSE(path.empty());
        CHECK(path.back() == SubCell{500, 100});
        CHECK_FALSE(crosses(f, path));
    }
    {
        // A band across the whole map: no way round, so the route crosses it, and costs more.
        Field f;
        slow_band(f, 0, kNodeW - 1);
        f.build({{500, 100}});
        const auto path = f.walk({500, 300});
        REQUIRE_FALSE(path.empty());
        CHECK(crosses(f, path));
        CHECK(f.field.dist({500, 300}) == 200 * kStraightCost + 21 * kStraightCost);
    }
}

TEST_CASE("targets: the nodes touching a footprint's edge from outside") {
    const SubCell core{400, 400};
    const auto around = nodes_around(core, kCoreSub);
    CHECK(around.size() == 4 * (kCoreSub + 1));
    for (const SubCell n : around) {
        CAPTURE(n.x);
        CAPTURE(n.y);
        // The node's 2 x 2 subcells don't overlap the core...
        const bool overlaps = n.x < core.x + kCoreSub && core.x < n.x + 2 &&
                              n.y < core.y + kCoreSub && core.y < n.y + 2;
        CHECK_FALSE(overlaps);
        // ...but share an edge with it.
        const bool beside_x = (n.x + 2 == core.x || n.x == core.x + kCoreSub) && n.y + 2 > core.y &&
                              n.y < core.y + kCoreSub;
        const bool beside_y = (n.y + 2 == core.y || n.y == core.y + kCoreSub) && n.x + 2 > core.x &&
                              n.x < core.x + kCoreSub;
        CHECK((beside_x || beside_y));
    }
}

TEST_CASE("breach: sealed in, the route goes through the cheapest wall") {
    // A wall across the map: blocks 1 (cheap) on the left, 2 (three times tougher) on the right.
    // From just right of the join, breaking the cheap one is quicker despite the walk.
    Field f;
    f.cost = {8000, 24000};
    f.wall(200, 203, 1, -10, 0, 0, 960);
    f.wall(200, 203, 2, -10, 0, 960, kSubW);
    f.build({{300, 100}});
    CHECK_FALSE(f.field.reached({1000, 300})); // sealed: no open route
    f.build({{300, 100}}, true);
    REQUIRE(f.field.reached({1000, 300}));
    const auto path = f.walk({1000, 300}, true);
    REQUIRE_FALSE(path.empty());
    CHECK(path.back() == SubCell{300, 100});
    // The first block the route enters.
    std::uint16_t first = 0;
    for (const SubCell n : path) {
        const NodeBlocks nb = node_blocks(f.blocks, n);
        if (nb.count > 0) {
            first = nb.id[0];
            break;
        }
    }
    CHECK(first == 1);
}

TEST_CASE("breach: a single wall is cheaper than a double one of the same blocks") {
    Field f;
    f.cost = {8000, 8000, 8000};
    f.wall(200, 203, 1, -10, 0, 0, 960);     // left: one wall
    f.wall(200, 203, 2, -10, 0, 960, kSubW); // right: two walls, one behind the other
    f.wall(203, 206, 3, -10, 0, 960, kSubW);
    f.build({{300, 100}}, true);
    const auto path = f.walk({1000, 300}, true);
    REQUIRE_FALSE(path.empty());
    bool through_double = false;
    for (const SubCell n : path) {
        const NodeBlocks nb = node_blocks(f.blocks, n);
        through_double = through_double || nb.has(2) || nb.has(3);
    }
    CHECK_FALSE(through_double);
}

TEST_CASE("the same inputs always give the same field") {
    Field a;
    Field b;
    for (Field* f : {&a, &b}) {
        f->cost = {5000};
        f->wall(300, 303, 1, 900, 2);
        for (int y = 500; y < 560; ++y) {
            for (int x = 200; x < 1700; ++x) {
                f->speed.at(x, y) = static_cast<std::uint8_t>(40 + (x * 7 + y * 3) % 60);
            }
        }
        f->build({{960, 100}, {961, 100}, {300, 900}});
    }
    CHECK(a.field.dist({1500, 1000}) == b.field.dist({1500, 1000}));
    CHECK(a.walk({1500, 1000}) == b.walk({1500, 1000}));
}
