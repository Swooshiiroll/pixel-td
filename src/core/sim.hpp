#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/flow.hpp"
#include "core/grid.hpp"
#include "core/los.hpp"
#include "core/map.hpp"
#include "core/sim_params.hpp"

namespace pixeltd {

// Phase 1 of pathfinding and mazing (#11; design/pathfinding.md): placeholder walkers find their
// own way from the spawns to the core over a flow field, blocks shape the route, walkers sealed in
// break through the cheapest way, and the core takes damage. Phase 2 (the test arena,
// design/test-arena.md) adds placeholder test towers that shoot projectiles, waves with summaries,
// and route rebuilds in the background. Advanced by tick() at 60 Hz; integer only, so the same
// commands always give the same state.

inline constexpr int kTicksPerSecond = 60;
inline constexpr int kFix = 256; // walker positions are in 1/256 px

enum class BlockKind : std::uint8_t { Light, Heavy };

[[nodiscard]] constexpr int block_size_sub(BlockKind k) noexcept {
    return k == BlockKind::Light ? kLightSub : kHeavySub;
}

// Which walker in reach a test tower shoots at. Ties go to the lowest walker id.
enum class Targeting : std::uint8_t {
    First,     // nearest the core along its route
    Closest,   // nearest the tower
    Strongest, // the most health
    Last,      // furthest from the core along its route
};
inline constexpr int kTargetings = 4;
[[nodiscard]] const char* targeting_name(Targeting t) noexcept;

// A test tower's gun and its readouts.
struct Gun {
    Targeting targeting = Targeting::First;
    int cooldown = 0;            // ticks until it can fire again
    std::uint32_t target = 0;    // the id of the walker it last shot at, or 0
    PointPx aim{};               // where it last aimed (the turret faces it)
    std::int64_t damage = 0;     // damage dealt in all
    int kills = 0;               //
    int shots = 0;               // fired in all
    std::array<int, 5> second{}; // damage in each of the last 5 seconds (a ring, by tick / 60)
};

// A block: a light (24 px) or heavy (48 px) footprint with health. An armed block is a test tower:
// it shoots too, and is placed, routed round and broken exactly like a block.
struct Block {
    SubCell top_left{};
    BlockKind kind = BlockKind::Light;
    int max_health = 0;
    std::int64_t health_ticks = 0; // health x 60, so a walker's damage per tick is whole
    bool alive = false;
    bool armed = false;       // a test tower
    std::uint32_t serial = 0; // tells a slot's towers apart, so shots credit the right one
    Gun gun{};

    [[nodiscard]] int size_sub() const noexcept { return block_size_sub(kind); }
    [[nodiscard]] int health() const noexcept {
        return static_cast<int>((health_ticks + kTicksPerSecond - 1) / kTicksPerSecond);
    }
};

enum class WalkerState : std::uint8_t {
    Walking,
    Breaking, // attacking the block in its way (sealed in)
    Stuck,    // no route at all (can't happen on a generated map)
    Waiting,  // its next step is into a block placed since the routes were built
};

// A placeholder enemy, 16 px across.
struct Walker {
    std::int32_t x = 0; // centre, in 1/256 px
    std::int32_t y = 0;
    int health = 0;
    int max_health = 0;
    int speed_scale_pct = 100; // its wave's speed multiplier
    std::uint32_t wave = 0;    // the wave it belongs to (0: a loose walker, from the stream)
    WalkerState state = WalkerState::Walking;
    SubCell node{};        // the route node it's at, or leaving
    SubCell next{};        // the node it's heading to (== node while it picks its next step)
    int target_block = -1; // the block slot it's breaking
    int speed_pct = 100;   // how fast it's going, as a % of full speed (slow ground)
    bool sealed = false;   // following the breach route
    bool arrived = false;  // reached the core this tick (removed at the end of it)
    bool dead = false;     // killed this tick (removed at the end of it)
    std::uint32_t id = 0;

    [[nodiscard]] PointPx centre() const noexcept { return {x / kFix, y / kFix}; }
};

// Why a block can or can't go somewhere.
enum class Place : std::uint8_t {
    Ok,
    Terrain, // off the map, or not buildable ground
    Core,    // overlaps the core
    Spawn,   // overlaps a spawn
    Block,   // overlaps another block
    Walker,  // a walker is standing there
};
[[nodiscard]] const char* place_name(Place p) noexcept;

// A spawn's route to the core, as the walkers would walk it now.
struct Route {
    std::vector<SubCell> nodes; // from the spawn's node to the core
    std::int32_t cost = 0;      // in step-cost units (see cost_ms)
    bool sealed = false;        // no open route: this is the breach route
    int breach_block = -1;      // the first block on a breach route (its slot)
};

// The route node a spawn's walkers start on: the middle of its 2 x 2 cells.
[[nodiscard]] constexpr SubCell spawn_node(const Spawn& s) noexcept {
    return {s.cell.x * kSubPerCell + 1, s.cell.y * kSubPerCell + 1};
}

// The centre of a route node, in world pixels.
[[nodiscard]] constexpr PointPx node_centre(SubCell n) noexcept {
    return {n.x * kSubPx + kSubPx, n.y * kSubPx + kSubPx};
}

// A test tower's shot in flight. Shots live in a pool: a dead one's slot is reused.
struct Projectile {
    std::int32_t x = 0; // in 1/256 px
    std::int32_t y = 0;
    std::int32_t vx = 0; // per tick, in 1/256 px
    std::int32_t vy = 0;
    std::int32_t travel = 0; // distance left before it's spent, in 1/256 px
    int damage = 0;
    int tower = -1;           // the block slot that fired it
    std::uint32_t serial = 0; // that tower's serial (the slot may be reused)
    bool alive = false;

    [[nodiscard]] PointPx centre() const noexcept { return {x / kFix, y / kFix}; }
};

// A wave to send: `count` walkers from each chosen spawn, `interval_ms` apart (0: all at once),
// with their health and speed scaled.
struct WaveSpec {
    int count = 10;
    int interval_ms = 1000;
    std::uint8_t spawn_mask = 0x7; // bit i: spawn i
    int health_pct = 100;
    int speed_pct = 100;

    friend constexpr bool operator==(const WaveSpec&, const WaveSpec&) = default;
};
// Clamps a wave into range; returns how many values changed.
int validate(WaveSpec& w) noexcept;

// What became of a wave.
struct WaveStats {
    std::uint32_t id = 0;
    WaveSpec spec{};
    int planned = 0; // walkers it sends in all
    int sent = 0;
    int alive = 0;
    int killed = 0;
    int leaked = 0; // reached the core
    int core_damage = 0;
    std::int64_t start_tick = 0;
    std::int64_t end_tick = -1; // when the last of it was killed or got through

    [[nodiscard]] bool done() const noexcept { return end_tick >= 0; }
};
inline constexpr std::size_t kWaveHistory = 8; // the summaries kept

class Sim {
  public:
    Sim();
    ~Sim();
    Sim(Sim&&) noexcept;
    Sim& operator=(Sim&&) noexcept;
    Sim(const Sim&) = delete;
    Sim& operator=(const Sim&) = delete;

    // Starts over on `map` (which must outlive the Sim, or until the next reset): no blocks,
    // towers, shots or walkers, the core at full health, the stream off.
    void reset(const Map& map, const SimParams& params);

    // New numbers (validated). Routes are rebuilt if the slowing or the break costs changed.
    // Placed blocks and towers keep their health; the towers' other numbers apply at once.
    void set_params(const SimParams& params);
    [[nodiscard]] const SimParams& params() const noexcept { return params_; }
    [[nodiscard]] const TowerParams& tower_params(BlockKind k) const noexcept {
        return k == BlockKind::Light ? params_.light_tower : params_.heavy_tower;
    }

    // Blocks and test towers share the same footprints, rules and slots.
    [[nodiscard]] Place can_place(SubCell top_left, BlockKind kind) const noexcept;
    Place place_block(SubCell top_left, BlockKind kind);
    Place place_tower(SubCell top_left, BlockKind kind);
    // Removes the block or tower covering subcell s. False if there's none.
    bool remove_block_at(SubCell s);
    // The slot of the block or tower covering subcell s, or -1.
    [[nodiscard]] int block_at(SubCell s) const noexcept;
    void set_targeting(int slot, Targeting t) noexcept;
    // A tower's damage per second over the last 5 seconds.
    [[nodiscard]] int tower_dps(int slot) const noexcept;

    // Sends one walker from every spawn: a wave of 1.
    std::uint32_t send_walkers();
    // Sends a wave (validated); its first walkers leave at once. Returns its id.
    std::uint32_t send_wave(const WaveSpec& spec);
    // The last kWaveHistory waves, oldest first.
    [[nodiscard]] const std::vector<WaveStats>& waves() const noexcept { return waves_; }
    // True while a wave still has walkers to send.
    [[nodiscard]] bool sending() const noexcept { return !queued_.empty(); }
    // The stream sends one loose walker per spawn every stream_interval_ms, the first right away.
    void set_stream(bool on) noexcept;
    [[nodiscard]] bool stream() const noexcept { return stream_; }

    // Routes. After a change on tick T, tick() rebuilds them on a worker thread from the state at
    // the end of T, and uses them from the start of tick T + route_delay_ticks; until then a
    // walker whose next step is into a new block waits. update_routes() rebuilds now, on this
    // thread (the first build always is).
    void update_routes();
    [[nodiscard]] bool routes_dirty() const noexcept { return dirty_; }
    [[nodiscard]] bool routes_pending() const noexcept { return pending_; }
    [[nodiscard]] int route_builds() const noexcept { return builds_; }
    // How many times a build wasn't done in time, so tick() waited for it.
    [[nodiscard]] int route_waits() const noexcept { return waits_; }
    // The last build's time, if a clock was given (diagnostics only; the sim never reads it).
    [[nodiscard]] std::int64_t last_build_ns() const noexcept { return last_build_ns_; }
    void set_clock(std::int64_t (*now_ns)()) noexcept { clock_ = now_ns; }
    // Off: builds run on this thread when they start (same results; for the tests).
    void set_threaded(bool on);

    // One 60 Hz step.
    void tick();

    [[nodiscard]] const std::vector<Walker>& walkers() const noexcept { return walkers_; }
    [[nodiscard]] const std::vector<Block>& blocks() const noexcept { return blocks_; }
    // The shot pool: only the alive ones are in flight.
    [[nodiscard]] const std::vector<Projectile>& projectiles() const noexcept {
        return projectiles_;
    }
    [[nodiscard]] int projectiles_in_flight() const noexcept { return in_flight_; }
    [[nodiscard]] int core_health() const noexcept { return core_health_; }
    [[nodiscard]] bool core_fallen() const noexcept { return core_health_ <= 0; }
    [[nodiscard]] std::int64_t ticks() const noexcept { return ticks_; }

    // The route a spawn's walkers would take now.
    [[nodiscard]] Route route_from(const Spawn& s) const;
    // How long a route of `cost` takes to walk, in milliseconds.
    [[nodiscard]] std::int64_t cost_ms(std::int32_t cost) const noexcept;
    // The walker whose centre is within 8 px of p (the nearest), or -1.
    [[nodiscard]] int walker_at(PointPx p) const noexcept;

    // A hash of the whole state, for the determinism tests.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

    [[nodiscard]] const FlowField& open_field() const noexcept { return open_; }
    // What the current routes were built from.
    [[nodiscard]] FlowInput input(bool breach) const noexcept;

  private:
    struct RouteBuild; // a build's inputs, results and worker thread
    struct Queued {    // a wave's walkers still to send
        std::uint32_t wave = 0;
        WaveSpec spec{};
        int remaining = 0;
        std::int64_t next_tick = 0;
        int interval_ticks = 1;
    };

    void build_speed();
    [[nodiscard]] std::int32_t break_cost(const Block& b) const noexcept;
    Place place(SubCell top_left, BlockKind kind, bool armed);
    void clear_block(int slot);
    void start_build();
    void apply_build();
    void join_build();
    void spawn_batch(const WaveSpec& spec, std::uint32_t wave);
    [[nodiscard]] WaveStats* wave_stats(std::uint32_t id) noexcept;
    [[nodiscard]] std::int32_t route_left(const Walker& w) const noexcept;
    void fire_towers();
    void move_projectiles();
    void remove_gone();
    // At a node: picks the next step (or arrives, attacks a block, waits, or finds none). True if
    // it has a step to walk.
    bool pick_step(Walker& w);
    void walk(Walker& w);

    const Map* map_ = nullptr;
    SimParams params_{};
    std::shared_ptr<const Grid<std::uint8_t>> speed_; // per route node: % of full speed, 0 = can't
    Grid<std::uint16_t> occupancy_;                   // per subcell: block slot + 1, or 0
    std::vector<Block> blocks_;                       // slots; a removed block's slot is reused
    std::vector<SubCell> targets_;                    // the nodes around the core
    // The current routes, and what they were built from.
    FlowField open_;
    FlowField breach_;
    bool breach_built_ = false;
    std::shared_ptr<const Grid<std::uint8_t>> field_speed_;
    Grid<std::uint16_t> field_blocks_;
    std::vector<std::int32_t> field_break_cost_;
    std::unique_ptr<RouteBuild> build_;
    bool threaded_ = true;
    bool dirty_ = true;
    bool pending_ = false;
    std::int64_t apply_tick_ = 0;
    int builds_ = 0;
    int waits_ = 0;
    std::int64_t last_build_ns_ = 0;
    std::int64_t (*clock_)() = nullptr;

    std::vector<Walker> walkers_;
    std::vector<Projectile> projectiles_;
    std::vector<int> free_projectiles_;
    int in_flight_ = 0;
    std::vector<WaveStats> waves_;
    std::vector<Queued> queued_;
    std::uint32_t next_wave_ = 1;
    int core_health_ = 0;
    std::int64_t ticks_ = 0;
    std::int64_t next_stream_ = 0;
    bool stream_ = false;
    std::uint32_t next_id_ = 1;
    std::uint32_t next_serial_ = 1;
};

} // namespace pixeltd
