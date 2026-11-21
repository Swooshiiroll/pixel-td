// The test arena's systems (#11 phase 2; design/test-arena.md): test towers, shots, waves and
// route builds in the background.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#include <doctest/doctest.h>

#include "core/mapgen.hpp"
#include "core/sim.hpp"

using namespace pixeltd;

namespace {

// A flat open map (all soil): the core at subcell 400,400 and one spawn to its west. Walkers from
// it walk east along route nodes y = 401 (pixel y 3216).
Map open_map() {
    Map m;
    m.core = SubCell{400, 400};
    m.spawns = {Spawn{Cell{100, 200}, false, 1}};
    return m;
}

template <class F> int run_until(Sim& sim, int limit, F done) {
    for (int i = 0; i < limit; ++i) {
        if (done()) {
            return i;
        }
        sim.tick();
    }
    return limit;
}

void run(Sim& sim, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        sim.tick();
    }
}

int alive_blocks(const Sim& sim) {
    return static_cast<int>(std::count_if(sim.blocks().begin(), sim.blocks().end(),
                                          [](const Block& b) { return b.alive; }));
}

const Block& tower(const Sim& sim, SubCell at) {
    const int slot = sim.block_at(at);
    REQUIRE(slot >= 0);
    return sim.blocks()[static_cast<std::size_t>(slot)];
}

// Seals the core in with light blocks (or test towers).
void ring_the_core(Sim& sim, bool towers) {
    for (int y = 397; y <= 409; y += 3) {
        for (int x = 397; x <= 409; x += 3) {
            const bool inside = x >= 400 && x <= 406 && y >= 400 && y <= 406;
            if (!inside) {
                const Place p = towers ? sim.place_tower({x, y}, BlockKind::Light)
                                       : sim.place_block({x, y}, BlockKind::Light);
                REQUIRE(p == Place::Ok);
            }
        }
    }
}

WaveSpec one(int health_pct = 100) {
    WaveSpec w;
    w.count = 1;
    w.interval_ms = 0;
    w.health_pct = health_pct;
    return w;
}

} // namespace

TEST_CASE("sim params: the test towers' and routes' numbers are validated") {
    SimParams p;
    p.light_tower.range_px = 0;
    p.heavy_tower.fire_interval_ms = 1;
    p.heavy_tower.shot_speed_px_s = 100000;
    p.route_delay_ticks = 500;
    CHECK(validate(p) == 4);
    CHECK(p.light_tower.range_px >= 8);
    CHECK(p.heavy_tower.fire_interval_ms >= 16);
    CHECK(p.heavy_tower.shot_speed_px_s <= 3000);
    CHECK(p.route_delay_ticks == 120);
    WaveSpec w;
    w.count = 0;
    w.spawn_mask = 0xff;
    w.speed_pct = 0;
    CHECK(validate(w) == 3);
    CHECK(w.count == 1);
    CHECK(w.spawn_mask == 0x7);
}

TEST_CASE("a test tower kills a walker standing still in exactly health / damage shots") {
    const Map m = open_map();
    SimParams p;
    p.light_block_health = 100000; // the walker keeps breaking its block
    p.light_tower.range_px = 400;
    Sim sim;
    sim.reset(m, p);
    ring_the_core(sim, false);
    sim.send_walkers();
    run_until(sim, 6000, [&] { return sim.walkers()[0].state == WalkerState::Breaking; });
    REQUIRE(sim.walkers()[0].state == WalkerState::Breaking);
    REQUIRE(sim.place_tower({380, 380}, BlockKind::Light) == Place::Ok);
    // 100 health, 10 a shot: 10 shots, 30 ticks apart, so the last leaves 270 ticks after the
    // first, plus its flight (about 200 px at 8 px a tick).
    const int ticks = run_until(sim, 1000, [&] { return sim.walkers().empty(); });
    CHECK(ticks > 270);
    CHECK(ticks < 270 + 40);
    const Block& t = tower(sim, {380, 380});
    CHECK(t.gun.shots == 10);
    CHECK(t.gun.kills == 1);
    CHECK(t.gun.damage == 100);
    CHECK(sim.tower_dps(sim.block_at({380, 380})) >= 16);
    CHECK(sim.tower_dps(sim.block_at({380, 380})) <= 20);
    CHECK(sim.waves().back().killed == 1);
    CHECK(sim.waves().back().done());
}

TEST_CASE("a test tower doesn't shoot what it can't see past rock") {
    // A tower 92 px south of the walkers' line, a row of rock between (cells y 203).
    const auto shots_with_rock = [](bool rock) {
        Map m = open_map();
        if (rock) {
            for (int x = 120; x <= 180; ++x) {
                m.rock.at(x, 203) = 100;
            }
        }
        SimParams p;
        p.light_tower.range_px = 200;
        Sim sim;
        sim.reset(m, p);
        REQUIRE(sim.place_tower({300, 412}, BlockKind::Light) == Place::Ok);
        sim.send_walkers();
        run_until(sim, 6000, [&] { return sim.walkers()[0].centre().x > 2800; });
        return tower(sim, {300, 412}).gun.shots;
    };
    CHECK(shots_with_rock(true) == 0);
    CHECK(shots_with_rock(false) > 0);
}

TEST_CASE("targeting: first, last, closest and strongest; ties go to the lowest id") {
    const Map m = open_map();
    // Three walkers in a line, ids 1, 2, 3 (1 ahead), with 100, 300 and 200 health; then a tower
    // south of them, nearest the last one.
    const auto first_target = [&](Targeting t, bool equal_health) {
        SimParams p;
        p.light_tower.range_px = 600;
        Sim sim;
        sim.reset(m, p);
        sim.send_wave(one(100));
        run(sim, 30);
        sim.send_wave(one(equal_health ? 100 : 300));
        run(sim, 30);
        sim.send_wave(one(equal_health ? 100 : 200));
        run(sim, 60);
        REQUIRE(sim.walkers().size() == 3);
        REQUIRE(sim.place_tower({200, 420}, BlockKind::Light) == Place::Ok);
        const int slot = sim.block_at({200, 420});
        sim.set_targeting(slot, t);
        sim.tick();
        return sim.blocks()[static_cast<std::size_t>(slot)].gun.target;
    };
    CHECK(first_target(Targeting::First, false) == 1);
    CHECK(first_target(Targeting::Last, false) == 3);
    CHECK(first_target(Targeting::Closest, false) == 3);
    CHECK(first_target(Targeting::Strongest, false) == 2);
    CHECK(first_target(Targeting::Strongest, true) == 1);
}

TEST_CASE("a test tower is a block: walkers route round it, and break it when sealed in") {
    const Map m = open_map();
    SimParams p;
    p.walker_health = 100000;
    Sim sim;
    sim.reset(m, p);
    sim.update_routes();
    const std::int32_t direct = sim.route_from(m.spawns[0]).cost;
    for (int y = 300; y < 510; y += 3) {
        REQUIRE(sim.place_tower({300, y}, BlockKind::Light) == Place::Ok);
    }
    sim.update_routes();
    CHECK(sim.route_from(m.spawns[0]).cost > direct);
    Sim sealed;
    sealed.reset(m, p);
    ring_the_core(sealed, true);
    sealed.update_routes();
    CHECK(sealed.route_from(m.spawns[0]).sealed);
    const int ring = alive_blocks(sealed);
    sealed.send_walkers();
    run_until(sealed, 6000, [&] { return alive_blocks(sealed) < ring; });
    CHECK(alive_blocks(sealed) == ring - 1);
}

TEST_CASE("shots: a slow shot misses a fast walker, and is spent at the end of its travel") {
    // A tower 76 px south of the line, reaching 90 px; shots at 1 px a tick, walkers at 4 px a
    // tick. A missed shot flies on north-west, into a row of rock just north of the line.
    const auto flight = [](bool rock) {
        Map m = open_map();
        if (rock) {
            for (int x = 120; x <= 165; ++x) {
                m.rock.at(x, 199) = 100;
            }
        }
        SimParams p;
        p.walker_speed_px_s = 256;
        p.light_tower.range_px = 90;
        p.light_tower.shot_speed_px_s = 60;
        p.light_tower.fire_interval_ms = 60000; // one shot
        Sim sim;
        sim.reset(m, p);
        REQUIRE(sim.place_tower({300, 410}, BlockKind::Light) == Place::Ok);
        sim.send_walkers();
        run_until(sim, 6000, [&] { return sim.projectiles_in_flight() > 0; });
        REQUIRE(sim.projectiles_in_flight() == 1);
        const int ticks = run_until(sim, 1000, [&] { return sim.projectiles_in_flight() == 0; });
        CHECK(tower(sim, {300, 410}).gun.damage == 0);
        return ticks;
    };
    const int open = flight(false);
    CHECK(open >= 130); // 1.5 x 90 px at 1 px a tick
    CHECK(open <= 140);
    const int into_rock = flight(true);
    CHECK(into_rock < open - 15);
}

TEST_CASE("shots: the pool doesn't grow past the most in flight at once") {
    const Map m = open_map();
    SimParams p;
    p.stream_interval_ms = 500;
    Sim sim;
    sim.reset(m, p);
    for (int x = 240; x <= 360; x += 30) {
        REQUIRE(sim.place_tower({x, 410}, BlockKind::Light) == Place::Ok);
        REQUIRE(sim.place_tower({x, 388}, BlockKind::Heavy) == Place::Ok);
    }
    sim.set_stream(true);
    int most = 0;
    for (int i = 0; i < 4000; ++i) {
        sim.tick();
        most = std::max(most, sim.projectiles_in_flight());
    }
    CHECK(most > 0);
    CHECK(static_cast<int>(sim.projectiles().size()) == most);
}

TEST_CASE("waves: count, interval, spawns, and health and speed scaling") {
    Map m = open_map();
    m.spawns.push_back(Spawn{Cell{100, 300}, false, 10});
    Sim sim;
    sim.reset(m, SimParams{});
    WaveSpec w;
    w.count = 4;
    w.interval_ms = 1000;
    w.spawn_mask = 0x2; // the second spawn only
    w.health_pct = 250;
    const std::uint32_t id = sim.send_wave(w);
    CHECK(sim.walkers().size() == 1);
    CHECK(sim.walkers()[0].max_health == 250);
    CHECK(sim.walkers()[0].node == spawn_node(m.spawns[1]));
    run(sim, 61);
    CHECK(sim.walkers().size() == 2);
    run(sim, 120);
    CHECK(sim.walkers().size() == 4);
    CHECK_FALSE(sim.sending());
    CHECK(sim.waves().back().id == id);
    CHECK(sim.waves().back().planned == 4);
    CHECK(sim.waves().back().sent == 4);
    // Twice the speed: twice as far in the same time.
    Sim fast;
    fast.reset(m, SimParams{});
    WaveSpec f = one();
    f.spawn_mask = 0x1;
    fast.send_wave(f);
    f.speed_pct = 200;
    fast.send_wave(f);
    run(fast, 300);
    const int start = spawn_node(m.spawns[0]).x * kSubPx + kSubPx;
    const int slow_d = fast.walkers()[0].centre().x - start;
    const int fast_d = fast.walkers()[1].centre().x - start;
    CHECK(fast_d >= 2 * slow_d - 2);
    CHECK(fast_d <= 2 * slow_d + 2);
    // A wave with no spawns chosen is done at once.
    WaveSpec none = one();
    none.spawn_mask = 0;
    sim.send_wave(none);
    CHECK(sim.waves().back().planned == 0);
    CHECK(sim.waves().back().done());
}

TEST_CASE("waves: the summary adds up (killed + leaked = sent), with the core damage") {
    const Map m = open_map();
    SimParams p;
    p.light_tower.fire_interval_ms = 1000; // too slow to stop them all
    p.light_tower.damage = 50;
    Sim sim;
    sim.reset(m, p);
    REQUIRE(sim.place_tower({300, 404}, BlockKind::Light) == Place::Ok);
    WaveSpec w;
    w.count = 8;
    w.interval_ms = 400;
    sim.send_wave(w);
    run_until(sim, 20000, [&] { return sim.waves().back().done(); });
    const WaveStats& s = sim.waves().back();
    REQUIRE(s.done());
    CHECK(s.sent == 8);
    CHECK(s.killed + s.leaked == 8);
    CHECK(s.killed > 0);
    CHECK(s.core_damage == s.leaked * p.walker_core_hit);
    CHECK(s.end_tick > s.start_tick);
    CHECK(sim.core_health() == p.core_health - s.core_damage);
    // Loose walkers (the stream) aren't counted in a wave.
    sim.set_stream(true);
    run(sim, 10);
    CHECK(sim.waves().size() == 1);
    CHECK(sim.walkers().back().wave == 0);
}

TEST_CASE("background routes: used exactly route_delay_ticks after the change") {
    const Map m = open_map();
    Sim sim;
    sim.reset(m, SimParams{});
    run(sim, 10);
    const int builds = sim.route_builds();
    REQUIRE(sim.place_block({600, 600}, BlockKind::Light) == Place::Ok);
    sim.tick(); // tick 10: the build starts at its end
    CHECK(sim.routes_pending());
    run_until(sim, 100, [&] { return sim.ticks() == 30; });
    CHECK(sim.route_builds() == builds);
    sim.tick(); // tick 30 = 10 + 20: it's used
    CHECK(sim.route_builds() == builds + 1);
    CHECK_FALSE(sim.routes_pending());
}

TEST_CASE("background routes: a walker waits at a new block until the new routes land") {
    const Map m = open_map();
    SimParams p;
    p.route_delay_ticks = 120;
    Sim sim;
    sim.reset(m, p);
    sim.send_walkers();
    run(sim, 200);
    const SubCell node = sim.walkers()[0].node;
    REQUIRE(node.y == 401);
    const SubCell at{node.x + 3, 400};
    REQUIRE(sim.place_block(at, BlockKind::Light) == Place::Ok);
    const RectPx r = rect_of(at, kLightSub);
    bool waited = false;
    for (int i = 0; i < 120; ++i) {
        sim.tick();
        const PointPx c = sim.walkers()[0].centre();
        // The walker's 16 px square never overlaps the block.
        CHECK_FALSE((c.x + 8 > r.x && c.x - 8 < r.x + r.w && c.y + 8 > r.y && c.y - 8 < r.y + r.h));
        waited = waited || sim.walkers()[0].state == WalkerState::Waiting;
    }
    CHECK(waited);
    run(sim, 10);
    CHECK(sim.walkers()[0].state == WalkerState::Walking);
    run_until(sim, 6000, [&] { return sim.walkers().empty(); });
    CHECK(sim.core_health() == p.core_health - p.walker_core_hit);
}

TEST_CASE("background routes: a change during a build is picked up by the next one") {
    const Map m = open_map();
    Sim sim;
    sim.reset(m, SimParams{});
    run(sim, 10);
    const int builds = sim.route_builds();
    REQUIRE(sim.place_block({300, 399}, BlockKind::Light) == Place::Ok);
    run(sim, 5);
    REQUIRE(sim.place_block({300, 402}, BlockKind::Light) == Place::Ok);
    run(sim, 60);
    CHECK(sim.route_builds() == builds + 2);
    Sim fresh;
    fresh.reset(m, SimParams{});
    REQUIRE(fresh.place_block({300, 399}, BlockKind::Light) == Place::Ok);
    REQUIRE(fresh.place_block({300, 402}, BlockKind::Light) == Place::Ok);
    fresh.update_routes();
    CHECK(sim.route_from(m.spawns[0]).cost == fresh.route_from(m.spawns[0]).cost);
}

TEST_CASE("determinism: threaded and same-thread builds give the same state, run after run") {
    const Map m = generate_map(1, GenParams{});
    REQUIRE(m.core.has_value());
    const auto play = [&](bool threaded) {
        Sim sim;
        sim.set_threaded(threaded);
        sim.reset(m, SimParams{});
        const SubCell c = *m.core;
        for (int i = 0; i < 6; ++i) {
            (void)sim.place_block({c.x - 6 - 3 * i, c.y + 12}, BlockKind::Light);
            (void)sim.place_tower({c.x - 6 - 3 * i, c.y - 6}, BlockKind::Light);
        }
        (void)sim.place_tower({c.x + 14, c.y - 3}, BlockKind::Heavy);
        sim.set_targeting(sim.block_at({c.x + 14, c.y - 3}), Targeting::Strongest);
        WaveSpec w;
        w.count = 12;
        w.interval_ms = 700;
        w.health_pct = 300;
        sim.send_wave(w);
        sim.set_stream(true);
        for (int i = 0; i < 1500; ++i) {
            sim.tick();
            if (i == 400) {
                (void)sim.remove_block_at({c.x - 6, c.y + 12});
            }
            if (i == 401 || i == 700) {
                (void)sim.place_block({c.x + 20, c.y + 20 + i / 100}, BlockKind::Heavy);
            }
        }
        return sim.state_hash();
    };
    const std::uint64_t a = play(true);
    CHECK(a == play(true));
    CHECK(a == play(false));
}

// Manual bench (#11 phase 2): a tick with 500 walkers and 50 test towers, and how often the sim
// waits for a background build. pixeltd_tests -tc="*arena bench*" --no-skip
TEST_CASE("arena bench (manual)" * doctest::skip()) {
    const Map m = generate_map(1, GenParams{});
    Sim sim;
    sim.set_clock([]() -> std::int64_t {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    });
    sim.reset(m, SimParams{});
    sim.update_routes();
    const SubCell c = *m.core;
    int towers = 0;
    // Two square rings round the core, with gaps between the towers.
    for (int d = 14; d <= 30 && towers < 50; d += 16) {
        for (int i = -d; i <= d && towers < 50; i += 5) {
            for (const SubCell at : {SubCell{c.x + i, c.y - d}, SubCell{c.x + i, c.y + d},
                                     SubCell{c.x - d, c.y + i}, SubCell{c.x + d, c.y + i}}) {
                if (towers < 50) {
                    towers += sim.place_tower(at, BlockKind::Light) == Place::Ok ? 1 : 0;
                }
            }
        }
    }
    sim.update_routes();
    SimParams tough;
    tough.walker_health = 100000;
    sim.set_params(tough);
    // 170 from each spawn, 50 ms apart, so they reach the towers spread out.
    WaveSpec wave;
    wave.count = 170;
    wave.interval_ms = 50;
    sim.send_wave(wave);
    run_until(sim, 30000, [&] { return sim.projectiles_in_flight() > 5; }); // into reach
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 60; ++i) {
        sim.tick();
    }
    MESSAGE(
        "tick with ", sim.walkers().size(), " walkers, ", towers, " towers, ",
        sim.projectiles_in_flight(), " shots: ",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() /
            60.0,
        " ms");
    // Blocks placed every half second for 10 s, ticking at the real 60 Hz: each starts a
    // background build.
    const int waits = sim.route_waits();
    const int builds = sim.route_builds();
    double worst = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 600; ++i) {
        std::this_thread::sleep_until(start + std::chrono::microseconds(16667LL * i));
        if (i % 30 == 0) {
            (void)sim.place_block({c.x - 60 + i / 10, c.y + 60}, BlockKind::Light);
        }
        t0 = std::chrono::steady_clock::now();
        sim.tick();
        worst = std::max(
            worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                       .count());
    }
    MESSAGE("route builds waited for: ", sim.route_waits() - waits, " of ",
            sim.route_builds() - builds, "; last build ",
            static_cast<double>(sim.last_build_ns()) / 1e6, " ms; worst tick ", worst, " ms");
}
