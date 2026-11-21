#include "core/sim.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

#include <doctest/doctest.h>

#include "core/mapgen.hpp"
#include "core/rng.hpp"

using namespace pixeltd;

namespace {

// A flat open map (all soil): the core at subcell 400,400 and one spawn to its west.
Map open_map() {
    Map m;
    m.core = SubCell{400, 400};
    m.spawns = {Spawn{Cell{100, 200}, false, 1}};
    return m;
}

// Runs ticks until `done` or `limit` ticks; returns how many ran.
template <class F> int run_until(Sim& sim, int limit, F done) {
    for (int i = 0; i < limit; ++i) {
        if (done()) {
            return i;
        }
        sim.tick();
    }
    return limit;
}

int alive_blocks(const Sim& sim) {
    return static_cast<int>(std::count_if(sim.blocks().begin(), sim.blocks().end(),
                                          [](const Block& b) { return b.alive; }));
}

// Light blocks all round the core (no gaps), sealing it in.
void ring_the_core(Sim& sim) {
    for (int y = 397; y <= 409; y += 3) {
        for (int x = 397; x <= 409; x += 3) {
            const bool inside = x >= 400 && x <= 406 && y >= 400 && y <= 406;
            if (!inside) {
                REQUIRE(sim.place_block({x, y}, BlockKind::Light) == Place::Ok);
            }
        }
    }
}

} // namespace

TEST_CASE("sim params: the defaults are valid; wild values are clamped") {
    SimParams p;
    CHECK(validate(p) == 0);
    CHECK(p == SimParams{});
    SimParams w;
    w.walker_speed_px_s = 0;
    w.walker_attack_per_s = -5;
    w.tree_speed_pct = 0;
    w.shallow_speed_pct = 500;
    w.stream_interval_ms = 1;
    CHECK(validate(w) == 5);
    CHECK(w.walker_speed_px_s >= 4);
    CHECK(w.walker_attack_per_s >= 1);
    CHECK(w.tree_speed_pct >= 10);
    CHECK(w.shallow_speed_pct <= 100);
    CHECK(validate(w) == 0);
}

TEST_CASE("slow ground: the slowest of the ground and a tree counts") {
    Map m;
    const SimParams p;
    m.ground.at(1, 1) = Ground::Shallow;
    m.ground.at(2, 1) = Ground::Mud;
    m.ground.at(3, 1) = Ground::Marsh;
    m.plant.at(4, 1) = Plant::Tree;
    m.ground.at(5, 1) = Ground::Mud;
    m.plant.at(5, 1) = Plant::Tree;
    m.ground.at(6, 1) = Ground::Deep;
    m.rock.at(7, 1) = 100;
    CHECK(speed_pct(m, {0, 1}, p) == 100);
    CHECK(speed_pct(m, {1, 1}, p) == 65);
    CHECK(speed_pct(m, {2, 1}, p) == 80);
    CHECK(speed_pct(m, {3, 1}, p) == 75);
    CHECK(speed_pct(m, {4, 1}, p) == 85);
    CHECK(speed_pct(m, {5, 1}, p) == 80);
    CHECK(speed_pct(m, {6, 1}, p) == 0);
    CHECK(speed_pct(m, {7, 1}, p) == 0);
}

TEST_CASE("a walker walks to the core and hits it") {
    const Map m = open_map();
    Sim sim;
    sim.reset(m, SimParams{});
    sim.send_walkers();
    REQUIRE(sim.walkers().size() == 1);
    // About 197 subcells (1576 px) at 32 px/s: about 49 s.
    const int ticks = run_until(sim, 4000, [&] { return sim.walkers().empty(); });
    CHECK(ticks > 45 * kTicksPerSecond);
    CHECK(ticks < 52 * kTicksPerSecond);
    CHECK(sim.core_health() == 1000 - 10);
}

TEST_CASE("sealed in, a walker breaks a block in health / attack seconds, then gets through") {
    const Map m = open_map();
    Sim sim;
    sim.reset(m, SimParams{});
    ring_the_core(sim);
    const int ring = alive_blocks(sim);
    CHECK(ring == 16);
    sim.update_routes();
    const Route route = sim.route_from(m.spawns[0]);
    CHECK(route.sealed);
    CHECK(route.breach_block >= 0);
    sim.send_walkers();
    run_until(sim, 4000, [&] { return sim.walkers()[0].state == WalkerState::Breaking; });
    REQUIRE(sim.walkers()[0].state == WalkerState::Breaking);
    CHECK(sim.walkers()[0].target_block == route.breach_block);
    // 200 health at 10 a second: 20 s.
    const int breaking = run_until(sim, 3000, [&] { return alive_blocks(sim) < ring; });
    CHECK(breaking >= 20 * kTicksPerSecond - 1);
    CHECK(breaking <= 20 * kTicksPerSecond + 1);
    run_until(sim, 600, [&] { return sim.walkers().empty(); });
    CHECK(sim.walkers().empty());
    CHECK(sim.core_health() == 990);
    // With a gap now, the route is open again.
    sim.update_routes();
    CHECK_FALSE(sim.route_from(m.spawns[0]).sealed);
}

TEST_CASE("placement: refused on the core, a spawn, a block, a walker or unbuildable ground") {
    Map m = open_map();
    m.ground.at(150, 150) = Ground::Shallow;
    m.plant.at(155, 150) = Plant::Tree;
    m.rock.at(160, 150) = 50;
    Sim sim;
    sim.reset(m, SimParams{});
    CHECK(sim.can_place({398, 398}, BlockKind::Light) == Place::Core);
    CHECK(sim.can_place({201, 401}, BlockKind::Light) == Place::Spawn);
    CHECK(sim.can_place({600, 300}, BlockKind::Light) == Place::Ok);
    CHECK(sim.place_block({600, 600}, BlockKind::Heavy) == Place::Ok);
    CHECK(sim.can_place({604, 604}, BlockKind::Light) == Place::Block);
    CHECK(sim.can_place({598, 598}, BlockKind::Light) == Place::Block); // one corner subcell
    CHECK(sim.can_place({606, 600}, BlockKind::Light) == Place::Ok);    // right beside it
    CHECK(sim.can_place({150 * 2, 150 * 2}, BlockKind::Light) == Place::Terrain); // shallow water
    CHECK(sim.can_place({155 * 2, 150 * 2}, BlockKind::Light) == Place::Terrain); // a tree
    CHECK(sim.can_place({160 * 2, 150 * 2}, BlockKind::Light) == Place::Terrain); // rock
    CHECK(sim.can_place({kSubW - 2, 10}, BlockKind::Light) == Place::Terrain);    // off the map
    CHECK(spawn_node(m.spawns[0]) == SubCell{201, 401});
    sim.send_walkers();
    for (int i = 0; i < 4 * kTicksPerSecond; ++i) {
        sim.tick();
    }
    // The walker is well away from its spawn (about 128 px): a block on it is refused, and so is
    // one just touching its 16 px square, but not one just clear of it.
    const PointPx c = sim.walkers()[0].centre();
    CHECK(c.x > (m.spawns[0].cell.x + 2) * kCellPx + 3 * kSubPx * 2);
    const SubCell at{c.x / kSubPx - 1, c.y / kSubPx - 1};
    CHECK(sim.can_place(at, BlockKind::Light) == Place::Walker);
    CHECK(sim.can_place({c.x / kSubPx + 1, c.y / kSubPx + 3}, BlockKind::Light) == Place::Ok);
    CHECK(sim.remove_block_at({603, 603}));
    CHECK_FALSE(sim.remove_block_at({603, 603}));
    CHECK(sim.can_place({600, 600}, BlockKind::Heavy) == Place::Ok);
}

TEST_CASE("blocks shape the route; removing one reroutes") {
    const Map m = open_map();
    Sim sim;
    sim.reset(m, SimParams{});
    sim.update_routes();
    const std::int32_t direct = sim.route_from(m.spawns[0]).cost;
    // A wall of light blocks straight across the way, from y 300 to 500, at x 300.
    for (int y = 300; y < 510; y += 3) {
        REQUIRE(sim.place_block({300, y}, BlockKind::Light) == Place::Ok);
    }
    sim.update_routes();
    const Route around = sim.route_from(m.spawns[0]);
    CHECK_FALSE(around.sealed);
    CHECK(around.cost > direct);
    // Take out the block in the middle of the straight line: the route goes back through.
    CHECK(sim.remove_block_at({300, 401}));
    sim.update_routes();
    CHECK(sim.route_from(m.spawns[0]).cost < around.cost);
}

TEST_CASE("the core's health stops at 0 and it reports fallen") {
    const Map m = open_map();
    SimParams p;
    p.walker_core_hit = 600;
    p.walker_speed_px_s = 256;
    Sim sim;
    sim.reset(m, p);
    sim.send_walkers();
    sim.send_walkers();
    run_until(sim, 2000, [&] { return sim.walkers().empty(); });
    CHECK(sim.core_health() == 0);
    CHECK(sim.core_fallen());
}

TEST_CASE("the stream sends a walker per spawn every interval") {
    const Map m = open_map();
    SimParams p;
    p.stream_interval_ms = 1000;
    Sim sim;
    sim.reset(m, p);
    sim.set_stream(true);
    for (int i = 0; i < 3 * kTicksPerSecond; ++i) {
        sim.tick();
    }
    CHECK(sim.walkers().size() == 3); // at 0, 1 and 2 s
    sim.set_stream(false);
    for (int i = 0; i < 3 * kTicksPerSecond; ++i) {
        sim.tick();
    }
    CHECK(sim.walkers().size() == 3);
}

TEST_CASE("determinism: the same commands give the same state") {
    const Map m = generate_map(1, GenParams{});
    REQUIRE(m.core.has_value());
    const auto run = [&] {
        Sim sim;
        sim.reset(m, SimParams{});
        // A few blocks just outside the core.
        const SubCell c = *m.core;
        for (int i = 0; i < 6; ++i) {
            (void)sim.place_block({c.x - 6 - 3 * i, c.y + 12}, BlockKind::Light);
        }
        (void)sim.place_block({c.x + 14, c.y - 3}, BlockKind::Heavy);
        sim.set_stream(true);
        for (int i = 0; i < 1500; ++i) {
            sim.tick();
            if (i == 700) {
                (void)sim.remove_block_at({c.x - 6, c.y + 12});
            }
        }
        return sim.state_hash();
    };
    const std::uint64_t a = run();
    CHECK(a == run());
}

TEST_CASE("fuzz: random valid numbers never break the sim") {
    const Map m = open_map();
    Rng rng(7);
    for (int round = 0; round < 8; ++round) {
        SimParams p;
        p.walker_speed_px_s = static_cast<int>(rng.next_below(300));
        p.walker_health = static_cast<int>(rng.next_below(1000));
        p.walker_attack_per_s = static_cast<int>(rng.next_below(500));
        p.walker_core_hit = static_cast<int>(rng.next_below(500));
        p.light_block_health = static_cast<int>(rng.next_below(5000));
        p.tree_speed_pct = static_cast<int>(rng.next_below(120));
        p.shallow_speed_pct = static_cast<int>(rng.next_below(120));
        p.stream_interval_ms = static_cast<int>(rng.next_below(5000));
        validate(p);
        Sim sim;
        sim.reset(m, p);
        ring_the_core(sim);
        sim.set_stream(true);
        for (int i = 0; i < 400; ++i) {
            sim.tick();
        }
        CHECK(sim.core_health() >= 0);
        CHECK(sim.core_health() <= p.core_health);
    }
}

// Manual bench (#11): the route rebuild on a real map, and a tick with 500 walkers.
// pixeltd_tests -tc="*route bench*" --no-skip
TEST_CASE("route bench (manual)" * doctest::skip()) {
    const Map m = generate_map(1, GenParams{});
    Sim sim;
    sim.reset(m, SimParams{});
    auto t0 = std::chrono::steady_clock::now();
    sim.update_routes();
    const double open_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("open route build (first): ", open_ms, " ms");
    // A rebuild, as after placing a block (the arrays already exist).
    const SubCell c = *m.core;
    (void)sim.place_block({c.x + 30, c.y + 30}, BlockKind::Light);
    t0 = std::chrono::steady_clock::now();
    sim.update_routes();
    MESSAGE(
        "open route rebuild: ",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
        " ms");
    // Seal the core, so the breach route is built too.
    for (int y = c.y - 3; y <= c.y + kCoreSub; y += 3) {
        for (int x = c.x - 3; x <= c.x + kCoreSub; x += 3) {
            (void)sim.place_block({x, y}, BlockKind::Light);
        }
    }
    t0 = std::chrono::steady_clock::now();
    sim.update_routes();
    MESSAGE(
        "open + breach build: ",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
        " ms");
    while (sim.walkers().size() < 500) {
        sim.send_walkers();
    }
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 60; ++i) {
        sim.tick();
    }
    MESSAGE(
        "tick with ", sim.walkers().size(), " walkers: ",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() /
            60.0,
        " ms");
}
