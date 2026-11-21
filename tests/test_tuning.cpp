// The arena's text formats (#11 phase 2): the live-tuning file and saved layouts.
#include <string>

#include <doctest/doctest.h>

#include "core/layout.hpp"
#include "core/rng.hpp"
#include "core/sim_tuning.hpp"

using namespace pixeltd;

TEST_CASE("tuning file: every value round-trips; the defaults read back as the defaults") {
    SimParams p;
    p.walker_speed_px_s = 40;
    p.shallow_speed_pct = 50;
    p.heavy_block_health = 900;
    p.light_tower.range_px = 99;
    p.heavy_tower.shot_speed_px_s = 321;
    p.route_delay_ticks = 7;
    REQUIRE(validate(p) == 0);
    SimParams q;
    std::string errors;
    CHECK(from_tuning(to_tuning(p), q, errors));
    CHECK(errors.empty());
    CHECK(q == p);
    SimParams d;
    CHECK(from_tuning(to_tuning(SimParams{}), d, errors));
    CHECK(d == SimParams{});
}

TEST_CASE("tuning file: unknown keys, bad values and out-of-range values are reported") {
    SimParams p;
    std::string errors;
    const std::string text = "# a comment\n"
                             "walker.health = 250   # trailing comment\n"
                             "walker.wings = 2\n"
                             "tower.light.damage = lots\n"
                             "tower.heavy.range_px = 100000\n"
                             "no equals sign\n";
    CHECK_FALSE(from_tuning(text, p, errors));
    CHECK(p.walker_health == 250);
    CHECK(p.light_tower.damage == SimParams{}.light_tower.damage);
    CHECK(p.heavy_tower.range_px == 960);
    CHECK(errors.find("line 3: unknown key 'walker.wings'") != std::string::npos);
    CHECK(errors.find("line 4: bad value for 'tower.light.damage'") != std::string::npos);
    CHECK(errors.find("line 6: expected key = value") != std::string::npos);
    CHECK(errors.find("clamped") != std::string::npos);
}

TEST_CASE("tuning file: random text never breaks it, and the result is always valid") {
    Rng rng(11);
    const std::string keys[] = {"walker.speed_px_s",
                                "tower.light.range_px",
                                "routes.delay_ticks",
                                "ground.mud_speed_pct",
                                "x",
                                ""};
    for (int round = 0; round < 200; ++round) {
        std::string text;
        for (int i = 0; i < 6; ++i) {
            text += keys[rng.next_below(6)];
            text += rng.next_below(4) == 0 ? " " : " = ";
            text += std::to_string(static_cast<int>(rng.next_below(200000)) - 100000);
            text += rng.next_below(5) == 0 ? "#\n" : "\n";
        }
        SimParams p;
        std::string errors;
        (void)from_tuning(text, p, errors);
        CHECK(validate(p) == 0);
    }
}

TEST_CASE("layouts: the seed, the map settings and every piece round-trip") {
    Layout l;
    l.seed = 0xdeadbeef12345ull;
    l.gen.mountain_share_pct = 31;
    l.gen.forest_noise.octaves = 2;
    l.pieces = {Piece{{10, 20}, BlockKind::Light, false, Targeting::First},
                Piece{{30, 40}, BlockKind::Heavy, true, Targeting::Strongest},
                Piece{{50, 60}, BlockKind::Light, true, Targeting::Last}};
    Layout r;
    std::string errors;
    CHECK(from_layout(to_layout(l), r, errors));
    CHECK(errors.empty());
    CHECK(r.seed == l.seed);
    CHECK(r.gen == l.gen);
    CHECK(r.pieces == l.pieces);
}

TEST_CASE("layouts: bad lines are reported and skipped") {
    Layout l;
    std::string errors;
    const std::string text = "seed = 42\n"
                             "block = light 5,6\n"
                             "block = medium 5,6\n"
                             "tower = heavy 7,8 sideways\n"
                             "tower = heavy 9,9\n"
                             "map.attempts = 9999999\n"
                             "colour = blue\n";
    CHECK_FALSE(from_layout(text, l, errors));
    CHECK(l.seed == 42);
    REQUIRE(l.pieces.size() == 2);
    CHECK(l.pieces[1].armed);
    CHECK(l.pieces[1].targeting == Targeting::First);
    CHECK(errors.find("line 3: bad value for 'block'") != std::string::npos);
    CHECK(errors.find("line 4: bad value for 'tower'") != std::string::npos);
    CHECK(errors.find("line 7: unknown key 'colour'") != std::string::npos);
    CHECK(errors.find("map settings") != std::string::npos);
}

TEST_CASE("layouts: a sim's pieces are captured and placed again the same") {
    Map m;
    m.core = SubCell{400, 400};
    m.spawns = {Spawn{Cell{100, 200}, false, 1}};
    Sim sim;
    sim.reset(m, SimParams{});
    REQUIRE(sim.place_block({600, 600}, BlockKind::Heavy) == Place::Ok);
    REQUIRE(sim.place_tower({300, 300}, BlockKind::Light) == Place::Ok);
    sim.set_targeting(sim.block_at({300, 300}), Targeting::Closest);
    const std::vector<Piece> pieces = pieces_of(sim);
    REQUIRE(pieces.size() == 2);
    Sim again;
    again.reset(m, SimParams{});
    CHECK(place_pieces(again, pieces) == 0);
    CHECK(pieces_of(again) == pieces);
    CHECK(place_pieces(again, pieces) == 2); // already taken
}
