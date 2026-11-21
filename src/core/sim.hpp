#pragma once

#include <cstdint>
#include <vector>

#include "core/flow.hpp"
#include "core/grid.hpp"
#include "core/los.hpp"
#include "core/map.hpp"
#include "core/sim_params.hpp"

namespace pixeltd {

// Phase 1 of pathfinding and mazing (#11; design/pathfinding.md): placeholder walkers find their
// own way from the spawns to the core over a flow field, blocks shape the route, walkers sealed in
// break through the cheapest way, and the core takes damage. Advanced by tick() at 60 Hz; integer
// only, so the same commands always give the same state.

inline constexpr int kTicksPerSecond = 60;
inline constexpr int kFix = 256; // walker positions are in 1/256 px

enum class BlockKind : std::uint8_t { Light, Heavy };

[[nodiscard]] constexpr int block_size_sub(BlockKind k) noexcept {
    return k == BlockKind::Light ? kLightSub : kHeavySub;
}

// A placeholder block (no weapon yet): a light (24 px) or heavy (48 px) footprint with health.
struct Block {
    SubCell top_left{};
    BlockKind kind = BlockKind::Light;
    int max_health = 0;
    std::int64_t health_ticks = 0; // health x 60, so a walker's damage per tick is whole
    bool alive = false;

    [[nodiscard]] int size_sub() const noexcept { return block_size_sub(kind); }
    [[nodiscard]] int health() const noexcept {
        return static_cast<int>((health_ticks + kTicksPerSecond - 1) / kTicksPerSecond);
    }
};

enum class WalkerState : std::uint8_t {
    Walking,
    Breaking, // attacking the block in its way (sealed in)
    Stuck,    // no route at all (can't happen on a generated map)
};

// A placeholder enemy, 16 px across.
struct Walker {
    std::int32_t x = 0; // centre, in 1/256 px
    std::int32_t y = 0;
    int health = 0;
    WalkerState state = WalkerState::Walking;
    SubCell node{};        // the route node it's at, or leaving
    SubCell next{};        // the node it's heading to (== node while it picks its next step)
    int target_block = -1; // the block slot it's breaking
    int speed_pct = 100;   // how fast it's going, as a % of full speed (slow ground)
    bool sealed = false;   // following the breach route
    bool arrived = false;  // reached the core this tick (removed at the end of it)
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

class Sim {
  public:
    // Starts over on `map` (which must outlive the Sim, or until the next reset): no blocks or
    // walkers, the core at full health, the stream off.
    void reset(const Map& map, const SimParams& params);

    // New numbers (validated). Routes are rebuilt if the slowing or the break costs changed.
    void set_params(const SimParams& params);
    [[nodiscard]] const SimParams& params() const noexcept { return params_; }

    [[nodiscard]] Place can_place(SubCell top_left, BlockKind kind) const noexcept;
    Place place_block(SubCell top_left, BlockKind kind);
    // Removes the block covering subcell s. False if there's none.
    bool remove_block_at(SubCell s);
    // The slot of the block covering subcell s, or -1.
    [[nodiscard]] int block_at(SubCell s) const noexcept;

    // Sends one walker from every spawn.
    void send_walkers();
    // The stream sends one walker per spawn every stream_interval_ms, the first right away.
    void set_stream(bool on) noexcept;
    [[nodiscard]] bool stream() const noexcept { return stream_; }

    // Rebuilds the routes if blocks or the numbers changed since. tick() calls it first; the game
    // may call it itself, to time it.
    void update_routes();
    [[nodiscard]] bool routes_dirty() const noexcept { return dirty_; }
    [[nodiscard]] int route_builds() const noexcept { return builds_; }

    // One 60 Hz step.
    void tick();

    [[nodiscard]] const std::vector<Walker>& walkers() const noexcept { return walkers_; }
    [[nodiscard]] const std::vector<Block>& blocks() const noexcept { return blocks_; }
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
    [[nodiscard]] FlowInput input(bool breach) const noexcept;

  private:
    void build_speed();
    [[nodiscard]] std::int32_t break_cost(const Block& b) const noexcept;
    void clear_block(int slot);
    // At a node: picks the next step (or arrives, attacks a block, or finds none). True if it has
    // a step to walk.
    bool pick_step(Walker& w);
    void walk(Walker& w);

    const Map* map_ = nullptr;
    SimParams params_{};
    Grid<std::uint8_t> speed_;      // per route node: % of full speed, 0 = can't stand there
    Grid<std::uint16_t> occupancy_; // per subcell: block slot + 1, or 0
    std::vector<Block> blocks_;     // slots; a removed block's slot is reused
    std::vector<std::int32_t> break_cost_;
    std::vector<SubCell> targets_; // the nodes around the core
    FlowField open_;
    FlowField breach_;
    bool breach_built_ = false;
    bool dirty_ = true;
    std::vector<Walker> walkers_;
    int core_health_ = 0;
    std::int64_t ticks_ = 0;
    std::int64_t next_stream_ = 0;
    bool stream_ = false;
    std::uint32_t next_id_ = 1;
    int builds_ = 0;
};

} // namespace pixeltd
