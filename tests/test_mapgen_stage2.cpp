#include "core/mapgen.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

#include <doctest/doctest.h>

#include "core/hash.hpp"

using namespace pixeltd;

namespace {

std::uint64_t route_hash(const Map& m) {
    Fnv1a h;
    h.add_int(m.core ? m.core->x : -1);
    h.add_int(m.core ? m.core->y : -1);
    h.add_int(m.spawn.x);
    h.add_int(m.spawn.y);
    for (const Cell c : m.spine) {
        h.add_int(c.x);
        h.add_int(c.y);
    }
    for (const std::uint8_t w : m.widths) {
        h.add_byte(w);
    }
    for (const PathKind k : m.path.cells()) {
        h.add_byte(static_cast<std::uint8_t>(k));
    }
    return h.value();
}

// The legal spots nearest the zone's four corners and its centre, without duplicates.
std::vector<Cell> sample_spots(const Map& m) {
    const std::vector<Cell> spots = legal_core_spots(m);
    const Rect z = m.zone;
    // Targets in doubled units (a spot's centre is 2 * top-left + 6).
    const int targets[5][2] = {{2 * z.x, 2 * z.y},
                               {2 * (z.x + z.w), 2 * z.y},
                               {2 * z.x, 2 * (z.y + z.h)},
                               {2 * (z.x + z.w), 2 * (z.y + z.h)},
                               {2 * z.x + z.w, 2 * z.y + z.h}};
    std::vector<Cell> out;
    for (const auto& t : targets) {
        const Cell* best = nullptr;
        long long best_d = 0;
        for (const Cell& s : spots) {
            const long long dx = 2 * s.x + kCoreSize - t[0];
            const long long dy = 2 * s.y + kCoreSize - t[1];
            const long long d = dx * dx + dy * dy;
            if (best == nullptr || d < best_d) {
                best = &s;
                best_d = d;
            }
        }
        if (best != nullptr && std::find(out.begin(), out.end(), *best) == out.end()) {
            out.push_back(*best);
        }
    }
    return out;
}

int chebyshev_to_core(Cell c, Cell core) {
    const int dx = std::max({core.x - c.x, 0, c.x - (core.x + kCoreSize - 1)});
    const int dy = std::max({core.y - c.y, 0, c.y - (core.y + kCoreSize - 1)});
    return std::max(dx, dy);
}

// Every rule a stage 2 route must keep (design/world.md, Path rules).
void check_route(const Map& m, Cell spot, const GenParams& p) {
    REQUIRE(m.core.has_value());
    REQUIRE(*m.core == spot);
    REQUIRE_FALSE(m.stats.stage2_fallback);

    const std::vector<Cell>& s = m.spine;
    REQUIRE(!s.empty());
    REQUIRE(s.front() == m.spawn);
    const Cell sp = m.spawn;
    REQUIRE((sp.x == 0 || sp.y == 0 || sp.x == kMapW - 2 || sp.y == kMapH - 2));
    REQUIRE(static_cast<int>(s.size()) >= p.path_min_len);
    REQUIRE(static_cast<int>(s.size()) <= p.path_max_len);

    REQUIRE(m.base_width >= p.base_width_min);
    REQUIRE(m.base_width <= p.base_width_max);
    REQUIRE(m.widths.size() == s.size());
    REQUIRE(std::all_of(m.widths.begin(), m.widths.end(),
                        [&](std::uint8_t w) { return w >= 2 && w <= p.wide_width_max; }));

    // A connected, simple centre line that ends beside the core.
    bool connected = true;
    for (std::size_t i = 1; i < s.size(); ++i) {
        connected = connected && std::abs(s[i].x - s[i - 1].x) + std::abs(s[i].y - s[i - 1].y) == 1;
    }
    REQUIRE(connected);
    std::vector<int> seen;
    for (const Cell c : s) {
        seen.push_back(c.y * kMapW + c.x);
    }
    std::sort(seen.begin(), seen.end());
    REQUIRE(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    std::vector<Cell> access;
    core_access_cells(spot, access);
    REQUIRE(std::find(access.begin(), access.end(), s.back()) != access.end());

    // Stretches that are far apart along the path (stretch_span cells or more) never come within
    // stretch_gap cells of each other on the map.
    Grid<int> at(kMapW, kMapH, -1);
    for (std::size_t i = 0; i < s.size(); ++i) {
        at.at(s[i]) = static_cast<int>(i);
    }
    bool apart = true;
    for (std::size_t i = 0; i < s.size() && apart; ++i) {
        const int r = p.stretch_gap - 1;
        for (int y = s[i].y - r; y <= s[i].y + r; ++y) {
            for (int x = s[i].x - r; x <= s[i].x + r; ++x) {
                apart = apart &&
                        (!at.in_bounds(x, y) || at.at(x, y) < static_cast<int>(i) + p.stretch_span);
            }
        }
    }
    REQUIRE(apart);

    // Path cells: ground, or the ramp of a high-ground core; never blockers or the core.
    const bool high_core = m.terrain.at(spot) == Terrain::High;
    const Rect core{spot.x, spot.y, kCoreSize, kCoreSize};
    std::optional<Cell> bad;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const PathKind k = m.path.at(x, y);
            if (k == PathKind::None) {
                continue;
            }
            const Terrain t = m.terrain.at(x, y);
            const bool ok = !core.contains(Cell{x, y}) && !is_blocker(t) &&
                            (t == Terrain::High ? high_core && k == PathKind::Ramp &&
                                                      chebyshev_to_core({x, y}, spot) <= p.ramp_max
                                                : k == PathKind::Path);
            if (!ok && !bad) {
                bad = Cell{x, y};
            }
        }
    }
    CAPTURE(bad ? bad->x : -1);
    CAPTURE(bad ? bad->y : -1);
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(build_space_ok(m, p));
}

void fill(Map& m, Rect r, Terrain t) {
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            m.terrain.at(x, y) = t;
        }
    }
}

} // namespace

TEST_CASE("stage 2 is deterministic per seed and core spot") {
    const Map base = generate_stage1(42);
    const Cell spot = *central_core_spot(base);
    Map a = base;
    Map b = base;
    REQUIRE(generate_stage2(a, spot));
    REQUIRE(generate_stage2(b, spot));
    CHECK(a.spine == b.spine);
    CHECK(a.widths == b.widths);
    CHECK(a.path == b.path);
    CHECK(a.spawn == b.spawn);
    CHECK(route_hash(a) == route_hash(b));
    // Running stage 2 again on the same map gives the same result too.
    REQUIRE(generate_stage2(a, spot));
    CHECK(route_hash(a) == route_hash(b));
}

TEST_CASE("different core spots give different routes") {
    const Map base = generate_stage1(7);
    const std::vector<Cell> spots = sample_spots(base);
    REQUIRE(spots.size() >= 2);
    Map a = base;
    Map b = base;
    REQUIRE(generate_stage2(a, spots.front()));
    REQUIRE(generate_stage2(b, spots.back()));
    CHECK(route_hash(a) != route_hash(b));
}

// Pinned like the stage 1 golden maps: update in the same pull request if the router changes.
TEST_CASE("golden stage 2 route") {
    Map m = generate_stage1(1);
    REQUIRE(generate_stage2(m, *central_core_spot(m)));
    CHECK(route_hash(m) == 0xcf29e47a5d74890aull);
}

TEST_CASE("an illegal core spot is refused and changes nothing") {
    Map m = generate_stage1(3);
    const Map before = m;
    CHECK_FALSE(generate_stage2(m, {m.zone.x - 1, m.zone.y}));
    CHECK_FALSE(m.core.has_value());
    CHECK(m.path == before.path);
}

TEST_CASE("a high-ground core gets a ramp, and only it climbs") {
    const GenParams p;
    Map m = make_fallback_map(11, p); // flat ground, zone near the right edge
    // A plateau over the zone's right part, its left edge 4 cells from the core.
    const Cell spot{m.zone.x + 8, m.zone.y + 5};
    fill(m, {spot.x - 4, 0, kMapW - (spot.x - 4), kMapH}, Terrain::High);
    REQUIRE(is_legal_core_spot(m, spot));
    REQUIRE(generate_stage2(m, spot, p));
    check_route(m, spot, p);
    int ramp = 0;
    for (const PathKind k : m.path.cells()) {
        ramp += k == PathKind::Ramp ? 1 : 0;
    }
    CHECK(ramp > 0);
}

TEST_CASE("light-tower spots") {
    Map m = make_fallback_map(2);
    CHECK(is_light_spot(m, {10, 10}));
    CHECK_FALSE(is_light_spot(m, {kMapW - 1, 10})); // off the map
    m.terrain.at(11, 11) = Terrain::High;
    CHECK_FALSE(is_light_spot(m, {10, 10})); // straddles heights
    m.terrain.at(11, 11) = Terrain::Rock;
    CHECK_FALSE(is_light_spot(m, {10, 10}));
    m.terrain.at(11, 11) = Terrain::Ground;
    m.path.at(10, 11) = PathKind::Path;
    CHECK_FALSE(is_light_spot(m, {10, 10}));
}

TEST_CASE("stage 2 sweep: 1,000 seeds, the zone's corners and centre") {
    const GenParams p;
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
        const Map base = generate_stage1(seed, p);
        for (const Cell spot : sample_spots(base)) {
            CAPTURE(seed);
            CAPTURE(spot.x);
            CAPTURE(spot.y);
            Map m = base;
            REQUIRE(generate_stage2(m, spot, p));
            check_route(m, spot, p);
        }
    }
}

// Manual tuning aid: pixeltd_tests -tc="*stage 2 stats*" --no-skip
TEST_CASE("stage 2 stats (manual)" * doctest::skip()) {
    const GenParams p;
    int routes = 0;
    int fallbacks = 0;
    long long attempts = 0;
    int max_attempts = 0;
    long long length = 0;
    long long target_gap = 0;
    long long ramps = 0;
    double max_ms = 0.0;
    double total_ms = 0.0;
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
        const Map base = generate_stage1(seed, p);
        for (const Cell spot : sample_spots(base)) {
            Map m = base;
            const auto t0 = std::chrono::steady_clock::now();
            generate_stage2(m, spot, p);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                    .count();
            total_ms += ms;
            max_ms = std::max(max_ms, ms);
            ++routes;
            fallbacks += m.stats.stage2_fallback ? 1 : 0;
            if (m.stats.stage2_fallback) {
                MESSAGE("fallback: seed ", seed, " spot ", spot.x, ",", spot.y, " target ",
                        m.target_length);
            }
            attempts += m.stats.stage2_attempts;
            max_attempts = std::max(max_attempts, m.stats.stage2_attempts);
            length += static_cast<long long>(m.spine.size());
            target_gap += std::abs(static_cast<int>(m.spine.size()) - m.target_length);
            for (const PathKind k : m.path.cells()) {
                ramps += k == PathKind::Ramp ? 1 : 0;
            }
        }
    }
    MESSAGE("routes ", routes, "  fallbacks ", fallbacks);
    MESSAGE("attempts avg ", static_cast<double>(attempts) / routes, " max ", max_attempts);
    MESSAGE("length avg ", static_cast<double>(length) / routes, "  |length - target| avg ",
            static_cast<double>(target_gap) / routes);
    MESSAGE("ms avg ", total_ms / routes, "  max ", max_ms, "  ramp cells ", ramps);
}
