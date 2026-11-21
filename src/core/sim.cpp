#include "core/sim.hpp"

#include <algorithm>

#include "core/hash.hpp"
#include "core/terrain.hpp"

namespace pixeltd {
namespace {

// Integer square root (floor), for walker steps.
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

} // namespace

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

void Sim::reset(const Map& map, const SimParams& params) {
    map_ = &map;
    params_ = params;
    validate(params_);
    occupancy_ = Grid<std::uint16_t>(kSubW, kSubH, 0);
    blocks_.clear();
    break_cost_.clear();
    walkers_.clear();
    core_health_ = params_.core_health;
    ticks_ = 0;
    next_stream_ = 0;
    stream_ = false;
    next_id_ = 1;
    targets_ = map.core ? nodes_around(*map.core, kCoreSub) : std::vector<SubCell>{};
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

void Sim::build_speed() {
    if (map_ == nullptr) {
        return;
    }
    // Each cell's speed, then each node's: the slowest of the (1, 2 or 4) cells under it; 0 if it
    // overlaps the core.
    Grid<std::uint8_t> cell(kMapW, kMapH, 0);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            cell.at(x, y) = static_cast<std::uint8_t>(speed_pct(*map_, Cell{x, y}, params_));
        }
    }
    speed_ = Grid<std::uint8_t>(kNodeW, kNodeH, 0);
    for (int y = 0; y < kNodeH; ++y) {
        for (int x = 0; x < kNodeW; ++x) {
            if (map_->core && overlap({x, y}, 2, *map_->core, kCoreSub)) {
                continue;
            }
            const int cx0 = x / kSubPerCell;
            const int cy0 = y / kSubPerCell;
            const int cx1 = (x + 1) / kSubPerCell;
            const int cy1 = (y + 1) / kSubPerCell;
            std::uint8_t s = std::min(std::min(cell.at(cx0, cy0), cell.at(cx1, cy0)),
                                      std::min(cell.at(cx0, cy1), cell.at(cx1, cy1)));
            speed_.at(x, y) = s;
        }
    }
}

FlowInput Sim::input(bool breach) const noexcept {
    return FlowInput{&speed_, &occupancy_, &break_cost_, breach};
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

Place Sim::place_block(SubCell top_left, BlockKind kind) {
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
        break_cost_.push_back(0);
    }
    Block& b = blocks_[slot];
    b.top_left = top_left;
    b.kind = kind;
    b.max_health =
        kind == BlockKind::Light ? params_.light_block_health : params_.heavy_block_health;
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

int Sim::block_at(SubCell s) const noexcept {
    if (!occupancy_.in_bounds(s.x, s.y)) {
        return -1;
    }
    return static_cast<int>(occupancy_.at(s.x, s.y)) - 1;
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

void Sim::send_walkers() {
    if (map_ == nullptr) {
        return;
    }
    for (const Spawn& s : map_->spawns) {
        Walker w;
        const SubCell n = spawn_node(s);
        const PointPx c = node_centre(n);
        w.x = c.x * kFix;
        w.y = c.y * kFix;
        w.health = params_.walker_health;
        w.node = n;
        w.next = n;
        w.id = next_id_++;
        walkers_.push_back(w);
    }
}

void Sim::set_stream(bool on) noexcept {
    stream_ = on;
    if (on) {
        next_stream_ = ticks_;
    }
}

void Sim::update_routes() {
    if (!dirty_ || map_ == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        break_cost_[i] = blocks_[i].alive ? break_cost(blocks_[i]) : 0;
    }
    open_.build(input(false), targets_);
    // The breach route is only needed when something is sealed in: a spawn, or a walker, with no
    // open route.
    bool sealed = false;
    for (const Spawn& s : map_->spawns) {
        sealed = sealed || !open_.reached(spawn_node(s));
    }
    for (const Walker& w : walkers_) {
        sealed = sealed || !open_.reached(w.node);
    }
    breach_built_ = sealed;
    if (sealed) {
        breach_.build(input(true), targets_);
    }
    // Every walker picks its next step again from the node it's at.
    for (Walker& w : walkers_) {
        w.next = w.node;
    }
    dirty_ = false;
    ++builds_;
}

bool Sim::pick_step(Walker& w) {
    // The open route if there is one, else the breach route.
    const bool open = open_.reached(w.node);
    if (!open && !(breach_built_ && breach_.reached(w.node))) {
        w.state = WalkerState::Stuck;
        return false;
    }
    const FlowField& field = open ? open_ : breach_;
    const FlowInput in = input(!open);
    w.sealed = !open;
    if (field.dist(w.node) == 0) {
        core_health_ = std::max(0, core_health_ - params_.walker_core_hit);
        w.arrived = true;
        return false;
    }
    const std::optional<SubCell> nx = field.next(in, w.node);
    if (!nx) {
        w.state = WalkerState::Stuck;
        return false;
    }
    if (!open) {
        // The step into a block: attack the first block it would enter.
        const NodeBlocks into = node_blocks(occupancy_, *nx);
        const NodeBlocks from = node_blocks(occupancy_, w.node);
        for (int i = 0; i < into.count; ++i) {
            if (from.has(into.id[i])) {
                continue;
            }
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
    }
    w.state = WalkerState::Walking;
    w.target_block = -1;
    w.next = *nx;
    return true;
}

void Sim::walk(Walker& w) {
    // A tick's worth of movement, as distance at full speed (x 100), carried on across the nodes
    // it reaches; each stretch is walked at the speed of the ground it leads onto.
    std::int64_t budget =
        static_cast<std::int64_t>(params_.walker_speed_px_s) * kFix * 100 / kTicksPerSecond;
    for (int guard = 0; guard < 8 && budget > 0; ++guard) {
        if (w.next == w.node && !pick_step(w)) {
            return;
        }
        const int pct = speed_.at(w.next.x, w.next.y);
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

void Sim::tick() {
    update_routes();
    if (stream_ && ticks_ >= next_stream_) {
        send_walkers();
        next_stream_ =
            ticks_ + static_cast<std::int64_t>(params_.stream_interval_ms) * kTicksPerSecond / 1000;
    }
    for (Walker& w : walkers_) {
        walk(w);
    }
    std::erase_if(walkers_, [](const Walker& w) { return w.arrived; });
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
            const NodeBlocks into = node_blocks(occupancy_, *nx);
            const NodeBlocks from = node_blocks(occupancy_, n);
            for (int i = 0; i < into.count; ++i) {
                if (!from.has(into.id[i])) {
                    r.breach_block = into.id[i] - 1;
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
    h.add_int(static_cast<int>(ticks_));
    h.add_int(core_health_);
    for (const Walker& w : walkers_) {
        h.add_int(w.x);
        h.add_int(w.y);
        h.add_int(w.health);
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
        h.add_int(static_cast<int>(b.health_ticks));
    }
    return h.value();
}

} // namespace pixeltd
