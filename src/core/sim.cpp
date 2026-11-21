#include "core/sim.hpp"

#include <algorithm>
#include <atomic>
#include <thread>
#include <utility>

#include "core/hash.hpp"
#include "core/terrain.hpp"

namespace pixeltd {
namespace {

// Integer square root (floor), for walker steps and shots.
std::int64_t isqrt(std::int64_t v) noexcept {
    if (v <= 0) {
        return 0;
    }
    auto x = static_cast<std::uint64_t>(v);
    std::uint64_t res = 0;
    std::uint64_t bit = 1ull << 62u;
    while (bit > x) {
        bit >>= 2u;
    }
    while (bit != 0) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1u) + bit;
        } else {
            res >>= 1u;
        }
        bit >>= 2u;
    }
    return static_cast<std::int64_t>(res);
}

// True if two subcell rectangles overlap.
constexpr bool overlap(SubCell a, int a_size, SubCell b, int b_size) noexcept {
    return a.x < b.x + b_size && b.x < a.x + a_size && a.y < b.y + b_size && b.y < a.y + a_size;
}

void fit(int& v, int lo, int hi, int& changed) {
    const int c = std::clamp(v, lo, hi);
    if (c != v) {
        v = c;
        ++changed;
    }
}

// Milliseconds to whole ticks (rounded), at least `lo`.
int ms_to_ticks(int ms, int lo) noexcept {
    return std::max(lo, (ms * kTicksPerSecond + 500) / 1000);
}

// A shot hits a walker whose centre is within this many pixels of it.
constexpr int kHitPx = 8;
// Shots move in steps of at most this much (1/256 px), so they can't pass through a walker.
constexpr std::int64_t kShotStep = static_cast<std::int64_t>(kSubPx) * kFix;
constexpr std::size_t kShotReserve = 4096;

} // namespace

// A route build: a snapshot of what routes are built from, the fields it builds, and the worker
// thread building them. On the heap, so a running build's address never changes.
struct Sim::RouteBuild {
    std::shared_ptr<const Grid<std::uint8_t>> speed;
    Grid<std::uint16_t> blocks;
    std::vector<std::int32_t> break_cost;
    std::vector<SubCell> targets;
    std::vector<SubCell> check; // nodes that need a route: the spawns and the walkers
    FlowField open;
    FlowField breach;
    bool breach_built = false;
    std::int64_t (*clock)() = nullptr;
    std::int64_t ns = 0;
    std::atomic<bool> done{false};
    std::thread worker;

    RouteBuild() = default;
    RouteBuild(const RouteBuild&) = delete;
    RouteBuild& operator=(const RouteBuild&) = delete;
    ~RouteBuild() {
        if (worker.joinable()) {
            worker.join();
        }
    }

    void run() {
        const std::int64_t t0 = clock != nullptr ? clock() : 0;
        FlowInput in{speed.get(), &blocks, &break_cost, false};
        open.build(in, targets);
        bool sealed = false;
        for (const SubCell n : check) {
            sealed = sealed || !open.reached(n);
        }
        breach_built = sealed;
        if (sealed) {
            in.breach = true;
            breach.build(in, targets);
        }
        ns = clock != nullptr ? clock() - t0 : 0;
        done.store(true, std::memory_order_release);
    }
};

const char* place_name(Place p) noexcept {
    switch (p) {
    case Place::Ok:
        return "ok";
    case Place::Terrain:
        return "not buildable ground";
    case Place::Core:
        return "on the core";
    case Place::Spawn:
        return "on a spawn";
    case Place::Block:
        return "on another block";
    case Place::Walker:
        return "a walker is there";
    }
    return "?";
}

const char* targeting_name(Targeting t) noexcept {
    switch (t) {
    case Targeting::First:
        return "first";
    case Targeting::Closest:
        return "closest";
    case Targeting::Strongest:
        return "strongest";
    case Targeting::Last:
        return "last";
    }
    return "?";
}

int validate(WaveSpec& w) noexcept {
    int changed = 0;
    fit(w.count, 1, 1000, changed);
    fit(w.interval_ms, 0, 60000, changed);
    fit(w.health_pct, 10, 10000, changed);
    fit(w.speed_pct, 10, 400, changed);
    if ((w.spawn_mask & 0x7u) != w.spawn_mask) {
        w.spawn_mask = static_cast<std::uint8_t>(w.spawn_mask & 0x7u);
        ++changed;
    }
    return changed;
}

Sim::Sim() : build_(std::make_unique<RouteBuild>()) {}

Sim::~Sim() = default;
Sim::Sim(Sim&&) noexcept = default;
Sim& Sim::operator=(Sim&&) noexcept = default;

void Sim::join_build() {
    if (build_ && build_->worker.joinable()) {
        build_->worker.join();
    }
}

void Sim::reset(const Map& map, const SimParams& params) {
    join_build();
    if (!build_) {
        build_ = std::make_unique<RouteBuild>();
    }
    map_ = &map;
    params_ = params;
    validate(params_);
    occupancy_ = Grid<std::uint16_t>(kSubW, kSubH, 0);
    blocks_.clear();
    walkers_.clear();
    projectiles_.clear();
    projectiles_.reserve(kShotReserve);
    free_projectiles_.clear();
    free_projectiles_.reserve(kShotReserve);
    in_flight_ = 0;
    waves_.clear();
    queued_.clear();
    next_wave_ = 1;
    core_health_ = params_.core_health;
    ticks_ = 0;
    next_stream_ = 0;
    stream_ = false;
    next_id_ = 1;
    next_serial_ = 1;
    targets_ = map.core ? nodes_around(*map.core, kCoreSub) : std::vector<SubCell>{};
    build_->targets = targets_;
    open_ = FlowField{};
    breach_ = FlowField{};
    breach_built_ = false;
    pending_ = false;
    waits_ = 0;
    build_speed();
    dirty_ = true;
}

void Sim::set_params(const SimParams& params) {
    SimParams p = params;
    validate(p);
    const bool ground = !same_ground_speeds(p, params_);
    const bool breaking = p.walker_attack_per_s != params_.walker_attack_per_s ||
                          p.walker_speed_px_s != params_.walker_speed_px_s;
    params_ = p;
    if (ground) {
        build_speed();
    }
    if (ground || breaking) {
        dirty_ = true;
    }
}

void Sim::set_threaded(bool on) {
    join_build();
    threaded_ = on;
}

void Sim::build_speed() {
    if (map_ == nullptr) {
        return;
    }
    // Each cell's speed, then each node's: the slowest of the (1, 2 or 4) cells under it; 0 if it
    // overlaps the core. A new grid each time, so a build still running keeps the one it has.
    Grid<std::uint8_t> cell(kMapW, kMapH, 0);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            cell.at(x, y) = static_cast<std::uint8_t>(speed_pct(*map_, Cell{x, y}, params_));
        }
    }
    auto speed = std::make_shared<Grid<std::uint8_t>>(kNodeW, kNodeH, std::uint8_t{0});
    for (int y = 0; y < kNodeH; ++y) {
        for (int x = 0; x < kNodeW; ++x) {
            if (map_->core && overlap({x, y}, 2, *map_->core, kCoreSub)) {
                continue;
            }
            const int cx0 = x / kSubPerCell;
            const int cy0 = y / kSubPerCell;
            const int cx1 = (x + 1) / kSubPerCell;
            const int cy1 = (y + 1) / kSubPerCell;
            speed->at(x, y) = std::min(std::min(cell.at(cx0, cy0), cell.at(cx1, cy0)),
                                       std::min(cell.at(cx0, cy1), cell.at(cx1, cy1)));
        }
    }
    speed_ = std::move(speed);
}

FlowInput Sim::input(bool breach) const noexcept {
    return FlowInput{field_speed_.get(), &field_blocks_, &field_break_cost_, breach};
}

std::int32_t Sim::break_cost(const Block& b) const noexcept {
    // Breaking takes health_ticks / (attack x 60) seconds; walking one subcell (8 px) costs 100
    // and takes 8 / speed seconds, so a second is worth 100 x speed / 8.
    const std::int64_t c =
        b.health_ticks * 100 * params_.walker_speed_px_s /
        (static_cast<std::int64_t>(params_.walker_attack_per_s) * kTicksPerSecond * kSubPx);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(c, 1, kMaxBreakCost));
}

Place Sim::can_place(SubCell top_left, BlockKind kind) const noexcept {
    const int s = block_size_sub(kind);
    if (map_ == nullptr || !placement_ok(*map_, top_left, s)) {
        return Place::Terrain;
    }
    if (map_->core && overlap(top_left, s, *map_->core, kCoreSub)) {
        return Place::Core;
    }
    for (const Spawn& sp : map_->spawns) {
        const SubCell st{sp.cell.x * kSubPerCell, sp.cell.y * kSubPerCell};
        if (overlap(top_left, s, st, 2 * kSubPerCell)) {
            return Place::Spawn;
        }
    }
    for (int y = top_left.y; y < top_left.y + s; ++y) {
        for (int x = top_left.x; x < top_left.x + s; ++x) {
            if (occupancy_.at(x, y) != 0) {
                return Place::Block;
            }
        }
    }
    // A walker's 16 px square against the block's footprint, in pixels, and the route nodes it's
    // between (so a block never lands on the step a walker is taking).
    const int bx0 = top_left.x * kSubPx;
    const int by0 = top_left.y * kSubPx;
    const int bx1 = bx0 + s * kSubPx;
    const int by1 = by0 + s * kSubPx;
    for (const Walker& w : walkers_) {
        const PointPx c = w.centre();
        if ((c.x + kSubPx > bx0 && c.x - kSubPx < bx1 && c.y + kSubPx > by0 &&
             c.y - kSubPx < by1) ||
            overlap(top_left, s, w.node, 2) || overlap(top_left, s, w.next, 2)) {
            return Place::Walker;
        }
    }
    return Place::Ok;
}

Place Sim::place(SubCell top_left, BlockKind kind, bool armed) {
    const Place p = can_place(top_left, kind);
    if (p != Place::Ok) {
        return p;
    }
    std::size_t slot = 0;
    while (slot < blocks_.size() && blocks_[slot].alive) {
        ++slot;
    }
    if (slot == blocks_.size()) {
        if (blocks_.size() >= 0xfffeu) {
            return Place::Block; // the occupancy grid's limit; far beyond any real maze
        }
        blocks_.emplace_back();
    }
    Block& b = blocks_[slot];
    b = Block{};
    b.top_left = top_left;
    b.kind = kind;
    b.armed = armed;
    b.serial = next_serial_++;
    if (armed) {
        b.max_health = tower_params(kind).health;
    } else {
        b.max_health =
            kind == BlockKind::Light ? params_.light_block_health : params_.heavy_block_health;
    }
    b.health_ticks = static_cast<std::int64_t>(b.max_health) * kTicksPerSecond;
    b.alive = true;
    const int s = b.size_sub();
    for (int y = top_left.y; y < top_left.y + s; ++y) {
        for (int x = top_left.x; x < top_left.x + s; ++x) {
            occupancy_.at(x, y) = static_cast<std::uint16_t>(slot + 1);
        }
    }
    dirty_ = true;
    return Place::Ok;
}

Place Sim::place_block(SubCell top_left, BlockKind kind) {
    return place(top_left, kind, false);
}

Place Sim::place_tower(SubCell top_left, BlockKind kind) {
    return place(top_left, kind, true);
}

int Sim::block_at(SubCell s) const noexcept {
    if (!occupancy_.in_bounds(s.x, s.y)) {
        return -1;
    }
    return static_cast<int>(occupancy_.at(s.x, s.y)) - 1;
}

void Sim::set_targeting(int slot, Targeting t) noexcept {
    if (slot >= 0 && static_cast<std::size_t>(slot) < blocks_.size()) {
        blocks_[static_cast<std::size_t>(slot)].gun.targeting = t;
    }
}

int Sim::tower_dps(int slot) const noexcept {
    if (slot < 0 || static_cast<std::size_t>(slot) >= blocks_.size()) {
        return 0;
    }
    int sum = 0;
    for (const int d : blocks_[static_cast<std::size_t>(slot)].gun.second) {
        sum += d;
    }
    return sum / static_cast<int>(Gun{}.second.size());
}

void Sim::clear_block(int slot) {
    Block& b = blocks_[static_cast<std::size_t>(slot)];
    const int s = b.size_sub();
    for (int y = b.top_left.y; y < b.top_left.y + s; ++y) {
        for (int x = b.top_left.x; x < b.top_left.x + s; ++x) {
            occupancy_.at(x, y) = 0;
        }
    }
    b.alive = false;
    b.health_ticks = 0;
    dirty_ = true;
}

bool Sim::remove_block_at(SubCell s) {
    const int slot = block_at(s);
    if (slot < 0) {
        return false;
    }
    clear_block(slot);
    return true;
}

WaveStats* Sim::wave_stats(std::uint32_t id) noexcept {
    if (id == 0) {
        return nullptr;
    }
    for (WaveStats& w : waves_) {
        if (w.id == id) {
            return &w;
        }
    }
    return nullptr;
}

void Sim::spawn_batch(const WaveSpec& spec, std::uint32_t wave) {
    if (map_ == nullptr) {
        return;
    }
    WaveStats* stats = wave_stats(wave);
    for (std::size_t i = 0; i < map_->spawns.size(); ++i) {
        if (i < 8 && (spec.spawn_mask & (1u << i)) == 0) {
            continue;
        }
        Walker w;
        const SubCell n = spawn_node(map_->spawns[i]);
        const PointPx c = node_centre(n);
        w.x = c.x * kFix;
        w.y = c.y * kFix;
        w.max_health =
            std::max(1, static_cast<int>(static_cast<std::int64_t>(params_.walker_health) *
                                         spec.health_pct / 100));
        w.health = w.max_health;
        w.speed_scale_pct = spec.speed_pct;
        w.wave = wave;
        w.node = n;
        w.next = n;
        w.id = next_id_++;
        walkers_.push_back(w);
        if (stats != nullptr) {
            ++stats->sent;
            ++stats->alive;
        }
    }
}

std::uint32_t Sim::send_walkers() {
    WaveSpec spec;
    spec.count = 1;
    spec.interval_ms = 0;
    return send_wave(spec);
}

std::uint32_t Sim::send_wave(const WaveSpec& in) {
    WaveSpec spec = in;
    validate(spec);
    WaveStats stats;
    stats.id = next_wave_++;
    stats.spec = spec;
    stats.start_tick = ticks_;
    int spawns = 0;
    if (map_ != nullptr) {
        for (std::size_t i = 0; i < map_->spawns.size(); ++i) {
            spawns += (i >= 8 || (spec.spawn_mask & (1u << i)) != 0) ? 1 : 0;
        }
    }
    stats.planned = spec.count * spawns;
    if (stats.planned == 0) {
        stats.end_tick = ticks_;
    }
    waves_.push_back(stats);
    if (waves_.size() > kWaveHistory) {
        waves_.erase(waves_.begin());
    }
    if (stats.planned == 0) {
        return stats.id;
    }
    const int interval = spec.interval_ms == 0 ? 0 : ms_to_ticks(spec.interval_ms, 1);
    if (interval == 0) {
        for (int i = 0; i < spec.count; ++i) {
            spawn_batch(spec, stats.id);
        }
    } else {
        spawn_batch(spec, stats.id);
        if (spec.count > 1) {
            queued_.push_back(Queued{stats.id, spec, spec.count - 1, ticks_ + interval, interval});
        }
    }
    return stats.id;
}

void Sim::set_stream(bool on) noexcept {
    stream_ = on;
    if (on) {
        next_stream_ = ticks_;
    }
}

void Sim::start_build() {
    RouteBuild& b = *build_;
    b.speed = speed_;
    b.blocks = occupancy_;
    b.break_cost.assign(blocks_.size(), 0);
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        b.break_cost[i] = blocks_[i].alive ? break_cost(blocks_[i]) : 0;
    }
    // The breach route is only needed when something is sealed in: a spawn, or a walker, with no
    // open route.
    b.check.clear();
    if (map_ != nullptr) {
        for (const Spawn& s : map_->spawns) {
            b.check.push_back(spawn_node(s));
        }
    }
    for (const Walker& w : walkers_) {
        b.check.push_back(w.node);
    }
    b.clock = clock_;
    b.done.store(false, std::memory_order_relaxed);
    dirty_ = false;
    pending_ = true;
    if (threaded_) {
        b.worker = std::thread([p = &b] { p->run(); });
    } else {
        b.run();
    }
}

void Sim::apply_build() {
    RouteBuild& b = *build_;
    if (b.worker.joinable()) {
        if (!b.done.load(std::memory_order_acquire)) {
            ++waits_;
        }
        b.worker.join();
    }
    std::swap(open_, b.open);
    std::swap(breach_, b.breach);
    breach_built_ = b.breach_built;
    field_speed_ = b.speed;
    std::swap(field_blocks_, b.blocks);
    std::swap(field_break_cost_, b.break_cost);
    last_build_ns_ = b.ns;
    pending_ = false;
    ++builds_;
    // Every walker picks its next step again from the node it's at. One that's now cut off with no
    // breach route asks for another build (it waits until then).
    for (Walker& w : walkers_) {
        w.next = w.node;
        if (!open_.reached(w.node) && !breach_built_) {
            dirty_ = true;
        }
    }
}

void Sim::update_routes() {
    if (map_ == nullptr) {
        return;
    }
    if (pending_) {
        apply_build();
    }
    if (!dirty_) {
        return;
    }
    const bool threaded = threaded_;
    threaded_ = false;
    start_build();
    threaded_ = threaded;
    apply_build();
}

std::int32_t Sim::route_left(const Walker& w) const noexcept {
    return (w.sealed ? breach_ : open_).dist(w.node);
}

bool Sim::pick_step(Walker& w) {
    // The open route if there is one, else the breach route.
    const bool open = open_.reached(w.node);
    if (!open && !(breach_built_ && breach_.reached(w.node))) {
        w.state = dirty_ || pending_ ? WalkerState::Waiting : WalkerState::Stuck;
        return false;
    }
    const FlowField& field = open ? open_ : breach_;
    const FlowInput in = input(!open);
    w.sealed = !open;
    if (field.dist(w.node) == 0) {
        const int before = core_health_;
        core_health_ = std::max(0, core_health_ - params_.walker_core_hit);
        if (WaveStats* s = wave_stats(w.wave); s != nullptr) {
            s->core_damage += before - core_health_;
        }
        w.arrived = true;
        return false;
    }
    const std::optional<SubCell> nx = field.next(in, w.node);
    if (!nx) {
        w.state = dirty_ || pending_ ? WalkerState::Waiting : WalkerState::Stuck;
        return false;
    }
    // The blocks standing there now (the routes may be older than the last change).
    const NodeBlocks into = node_blocks(occupancy_, *nx);
    const NodeBlocks from = node_blocks(occupancy_, w.node);
    for (int i = 0; i < into.count; ++i) {
        if (from.has(into.id[i])) {
            continue;
        }
        if (open) {
            // A block placed since the routes were built: wait for the new ones.
            w.state = WalkerState::Waiting;
            w.target_block = -1;
            return false;
        }
        // Sealed in: attack the first block it would enter.
        const int slot = into.id[i] - 1;
        Block& b = blocks_[static_cast<std::size_t>(slot)];
        w.state = WalkerState::Breaking;
        w.target_block = slot;
        b.health_ticks -= params_.walker_attack_per_s;
        if (b.health_ticks <= 0) {
            clear_block(slot);
            w.state = WalkerState::Walking;
            w.target_block = -1;
        }
        return false;
    }
    w.state = WalkerState::Walking;
    w.target_block = -1;
    w.next = *nx;
    return true;
}

void Sim::walk(Walker& w) {
    // A tick's worth of movement, as distance at full speed (x 100), carried on across the nodes
    // it reaches; each stretch is walked at the speed of the ground it leads onto.
    std::int64_t budget = static_cast<std::int64_t>(params_.walker_speed_px_s) * w.speed_scale_pct *
                          kFix / kTicksPerSecond;
    for (int guard = 0; guard < 16 && budget > 0; ++guard) {
        if (w.next == w.node && !pick_step(w)) {
            return;
        }
        const int pct = speed_->at(w.next.x, w.next.y);
        if (pct == 0) {
            return; // can't happen: routes only lead onto walkable nodes
        }
        w.speed_pct = pct;
        const std::int64_t reach = std::max<std::int64_t>(1, budget * pct / 10000);
        const PointPx t = node_centre(w.next);
        const std::int64_t dx = static_cast<std::int64_t>(t.x) * kFix - w.x;
        const std::int64_t dy = static_cast<std::int64_t>(t.y) * kFix - w.y;
        const std::int64_t d = isqrt(dx * dx + dy * dy);
        if (d <= reach) {
            w.x = t.x * kFix;
            w.y = t.y * kFix;
            w.node = w.next;
            budget -= d * 10000 / pct;
        } else {
            w.x += static_cast<std::int32_t>(dx * reach / d);
            w.y += static_cast<std::int32_t>(dy * reach / d);
            budget = 0;
        }
    }
}

void Sim::fire_towers() {
    for (std::size_t slot = 0; slot < blocks_.size(); ++slot) {
        Block& b = blocks_[slot];
        if (!b.alive || !b.armed) {
            continue;
        }
        Gun& g = b.gun;
        if (g.cooldown > 0) {
            --g.cooldown;
        }
        if (g.cooldown > 0) {
            continue;
        }
        const TowerParams& tp = tower_params(b.kind);
        const PointPx c = centre_of(b.top_left, b.size_sub());
        // The best walker in reach and in sight by the tower's targeting; ties keep the first
        // found, the lowest id (walkers stay in id order). Sight is only checked for a walker that
        // would be the best so far.
        int best = -1;
        std::int64_t best_key = 0;
        for (std::size_t i = 0; i < walkers_.size(); ++i) {
            const Walker& w = walkers_[i];
            if (w.dead || w.arrived) {
                continue;
            }
            const PointPx wc = w.centre();
            const std::int64_t dx = wc.x - c.x;
            const std::int64_t dy = wc.y - c.y;
            if (!in_reach(tp.range_px, dx, dy)) {
                continue;
            }
            std::int64_t key = 0;
            switch (g.targeting) {
            case Targeting::First:
                key = route_left(w);
                break;
            case Targeting::Last:
                key = -static_cast<std::int64_t>(route_left(w));
                break;
            case Targeting::Closest:
                key = dx * dx + dy * dy;
                break;
            case Targeting::Strongest:
                key = -static_cast<std::int64_t>(w.health);
                break;
            }
            if ((best < 0 || key < best_key) && can_see(*map_, c, wc)) {
                best = static_cast<int>(i);
                best_key = key;
            }
        }
        if (best < 0) {
            continue;
        }
        const Walker& target = walkers_[static_cast<std::size_t>(best)];
        const PointPx t = target.centre();
        const std::int64_t dx = t.x - c.x;
        const std::int64_t dy = t.y - c.y;
        const std::int64_t d = std::max<std::int64_t>(1, isqrt(dx * dx + dy * dy));
        const std::int64_t per_tick = static_cast<std::int64_t>(tp.shot_speed_px_s) * kFix /
                                      kTicksPerSecond; // 1/256 px a tick
        Projectile p;
        p.x = c.x * kFix;
        p.y = c.y * kFix;
        p.vx = static_cast<std::int32_t>(dx * per_tick / d);
        p.vy = static_cast<std::int32_t>(dy * per_tick / d);
        p.travel = static_cast<std::int32_t>(static_cast<std::int64_t>(tp.range_px) * 3 / 2 * kFix);
        p.damage = tp.damage;
        p.tower = static_cast<int>(slot);
        p.serial = b.serial;
        p.alive = true;
        if (!free_projectiles_.empty()) {
            projectiles_[static_cast<std::size_t>(free_projectiles_.back())] = p;
            free_projectiles_.pop_back();
        } else {
            projectiles_.push_back(p);
        }
        ++in_flight_;
        g.cooldown = ms_to_ticks(tp.fire_interval_ms, 1);
        g.target = target.id;
        g.aim = t;
        ++g.shots;
    }
}

void Sim::move_projectiles() {
    const auto bucket = static_cast<std::size_t>((ticks_ / kTicksPerSecond) % 5);
    for (std::size_t i = 0; i < projectiles_.size(); ++i) {
        Projectile& p = projectiles_[i];
        if (!p.alive) {
            continue;
        }
        const std::int64_t len =
            isqrt(static_cast<std::int64_t>(p.vx) * p.vx + static_cast<std::int64_t>(p.vy) * p.vy);
        const std::int64_t steps = std::max<std::int64_t>(1, (len + kShotStep - 1) / kShotStep);
        const std::int64_t x0 = p.x;
        const std::int64_t y0 = p.y;
        bool spent = false;
        for (std::int64_t k = 1; k <= steps && !spent; ++k) {
            const std::int64_t sx = x0 + p.vx * k / steps;
            const std::int64_t sy = y0 + p.vy * k / steps;
            const int px = static_cast<int>(sx / kFix);
            const int py = static_cast<int>(sy / kFix);
            if (sx < 0 || sy < 0 || px >= kMapPxW || py >= kMapPxH ||
                is_rock(*map_, Cell{px / kCellPx, py / kCellPx})) {
                spent = true; // off the map, or into rock (direct fire)
                break;
            }
            for (Walker& w : walkers_) {
                if (w.dead || w.arrived) {
                    continue;
                }
                const PointPx wc = w.centre();
                const std::int64_t dx = wc.x - px;
                const std::int64_t dy = wc.y - py;
                if (dx * dx + dy * dy > static_cast<std::int64_t>(kHitPx) * kHitPx) {
                    continue;
                }
                const int dealt = std::min(p.damage, w.health);
                w.health -= p.damage;
                const bool killed = w.health <= 0;
                if (killed) {
                    w.health = 0;
                    w.dead = true;
                }
                Block& b = blocks_[static_cast<std::size_t>(p.tower)];
                if (b.alive && b.armed && b.serial == p.serial) {
                    b.gun.damage += dealt;
                    b.gun.second[bucket] += dealt;
                    b.gun.kills += killed ? 1 : 0;
                }
                spent = true;
                break;
            }
        }
        if (!spent) {
            p.x += p.vx;
            p.y += p.vy;
            p.travel -= static_cast<std::int32_t>(len);
            spent = p.travel <= 0;
        }
        if (spent) {
            p.alive = false;
            free_projectiles_.push_back(static_cast<int>(i));
            --in_flight_;
        }
    }
}

void Sim::remove_gone() {
    for (const Walker& w : walkers_) {
        if (!w.arrived && !w.dead) {
            continue;
        }
        if (WaveStats* s = wave_stats(w.wave); s != nullptr) {
            ++(w.dead ? s->killed : s->leaked);
            --s->alive;
        }
    }
    std::erase_if(walkers_, [](const Walker& w) { return w.arrived || w.dead; });
    for (WaveStats& s : waves_) {
        if (!s.done() && s.sent == s.planned && s.alive == 0) {
            s.end_tick = ticks_;
        }
    }
}

void Sim::tick() {
    // Routes: the first build, or with no delay, now; otherwise a background build is used from
    // its tick on.
    if (!open_.built() || (params_.route_delay_ticks == 0 && (dirty_ || pending_))) {
        update_routes();
    } else if (pending_ && ticks_ >= apply_tick_) {
        apply_build();
    }
    // A new second: start its bucket of the towers' damage readouts afresh.
    if (ticks_ % kTicksPerSecond == 0) {
        const auto bucket = static_cast<std::size_t>((ticks_ / kTicksPerSecond) % 5);
        for (Block& b : blocks_) {
            b.gun.second[bucket] = 0;
        }
    }
    if (stream_ && ticks_ >= next_stream_) {
        WaveSpec loose;
        spawn_batch(loose, 0);
        next_stream_ =
            ticks_ + static_cast<std::int64_t>(params_.stream_interval_ms) * kTicksPerSecond / 1000;
    }
    for (Queued& q : queued_) {
        while (q.remaining > 0 && ticks_ >= q.next_tick) {
            spawn_batch(q.spec, q.wave);
            --q.remaining;
            q.next_tick += q.interval_ticks;
        }
    }
    std::erase_if(queued_, [](const Queued& q) { return q.remaining <= 0; });
    fire_towers();
    move_projectiles();
    for (Walker& w : walkers_) {
        if (!w.dead) {
            walk(w);
        }
    }
    remove_gone();
    // A change this tick starts a background build, used from tick T + delay.
    if (dirty_ && !pending_ && params_.route_delay_ticks > 0) {
        start_build();
        apply_tick_ = ticks_ + params_.route_delay_ticks;
    }
    ++ticks_;
}

Route Sim::route_from(const Spawn& s) const {
    Route r;
    const SubCell start = spawn_node(s);
    const bool open = open_.reached(start);
    if (!open && !(breach_built_ && breach_.reached(start))) {
        return r;
    }
    const FlowField& field = open ? open_ : breach_;
    const FlowInput in = input(!open);
    r.sealed = !open;
    r.cost = field.dist(start);
    SubCell n = start;
    r.nodes.push_back(n);
    for (int guard = 0; guard < kNodeW * 4; ++guard) {
        const std::optional<SubCell> nx = field.next(in, n);
        if (!nx) {
            break;
        }
        if (r.sealed && r.breach_block < 0) {
            const NodeBlocks into = node_blocks(field_blocks_, *nx);
            const NodeBlocks from = node_blocks(field_blocks_, n);
            for (int i = 0; i < into.count; ++i) {
                if (!from.has(into.id[i])) {
                    r.breach_block = into.id[i] - 1;
                    if (!blocks_[static_cast<std::size_t>(r.breach_block)].alive) {
                        r.breach_block = -1; // broken since the routes were built
                    }
                    break;
                }
            }
        }
        n = *nx;
        r.nodes.push_back(n);
    }
    return r;
}

std::int64_t Sim::cost_ms(std::int32_t cost) const noexcept {
    return static_cast<std::int64_t>(cost) * kSubPx * 1000 /
           (100 * static_cast<std::int64_t>(params_.walker_speed_px_s));
}

int Sim::walker_at(PointPx p) const noexcept {
    int best = -1;
    std::int64_t best_d2 = static_cast<std::int64_t>(kSubPx) * kSubPx + 1;
    for (std::size_t i = 0; i < walkers_.size(); ++i) {
        const PointPx c = walkers_[i].centre();
        const std::int64_t dx = c.x - p.x;
        const std::int64_t dy = c.y - p.y;
        const std::int64_t d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best = static_cast<int>(i);
            best_d2 = d2;
        }
    }
    return best;
}

std::uint64_t Sim::state_hash() const noexcept {
    Fnv1a h;
    const auto add64 = [&h](std::int64_t v) {
        h.add_u32(static_cast<std::uint32_t>(v));
        h.add_u32(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v) >> 32u));
    };
    add64(ticks_);
    h.add_int(core_health_);
    h.add_int(pending_ ? 1 : 0);
    add64(apply_tick_);
    h.add_int(builds_);
    for (const Walker& w : walkers_) {
        h.add_int(w.x);
        h.add_int(w.y);
        h.add_int(w.health);
        h.add_int(w.max_health);
        h.add_int(w.speed_scale_pct);
        h.add_u32(w.wave);
        h.add_int(static_cast<int>(w.state));
        h.add_int(w.node.x);
        h.add_int(w.node.y);
        h.add_int(w.next.x);
        h.add_int(w.next.y);
        h.add_int(w.target_block);
        h.add_u32(w.id);
    }
    for (const Block& b : blocks_) {
        h.add_int(b.alive ? 1 : 0);
        h.add_int(b.top_left.x);
        h.add_int(b.top_left.y);
        h.add_int(static_cast<int>(b.kind));
        add64(b.health_ticks);
        h.add_int(b.armed ? 1 : 0);
        h.add_u32(b.serial);
        h.add_int(static_cast<int>(b.gun.targeting));
        h.add_int(b.gun.cooldown);
        add64(b.gun.damage);
        h.add_int(b.gun.kills);
        h.add_int(b.gun.shots);
    }
    for (const Projectile& p : projectiles_) {
        if (!p.alive) {
            continue;
        }
        h.add_int(p.x);
        h.add_int(p.y);
        h.add_int(p.vx);
        h.add_int(p.vy);
        h.add_int(p.travel);
        h.add_int(p.tower);
    }
    for (const Queued& q : queued_) {
        h.add_u32(q.wave);
        h.add_int(q.remaining);
        add64(q.next_tick);
    }
    for (const WaveStats& s : waves_) {
        h.add_u32(s.id);
        h.add_int(s.sent);
        h.add_int(s.killed);
        h.add_int(s.leaked);
        h.add_int(s.core_damage);
        add64(s.end_tick);
    }
    return h.value();
}

} // namespace pixeltd
