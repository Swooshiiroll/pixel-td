#include "core/sim_tuning.hpp"

#include <array>
#include <string>

#include "core/text.hpp"

namespace pixeltd {
namespace {

struct IntKey {
    const char* section; // the comment line it starts, or nullptr
    const char* key;
    int SimParams::* field;
};

constexpr std::array<IntKey, 12> kInts = {{
    {"Walkers", "walker.speed_px_s", &SimParams::walker_speed_px_s},
    {nullptr, "walker.health", &SimParams::walker_health},
    {nullptr, "walker.attack_per_s", &SimParams::walker_attack_per_s},
    {nullptr, "walker.core_hit", &SimParams::walker_core_hit},
    {"Slow ground: % of full speed", "ground.tree_speed_pct", &SimParams::tree_speed_pct},
    {nullptr, "ground.mud_speed_pct", &SimParams::mud_speed_pct},
    {nullptr, "ground.marsh_speed_pct", &SimParams::marsh_speed_pct},
    {nullptr, "ground.shallow_speed_pct", &SimParams::shallow_speed_pct},
    {"Blocks and the core", "block.light.health", &SimParams::light_block_health},
    {nullptr, "block.heavy.health", &SimParams::heavy_block_health},
    {nullptr, "core.health", &SimParams::core_health},
    {"Stream", "stream.interval_ms", &SimParams::stream_interval_ms},
}};

struct TowerKey {
    const char* prefix;
    TowerParams SimParams::* field;
};
constexpr std::array<TowerKey, 2> kTowers = {{
    {"tower.light.", &SimParams::light_tower},
    {"tower.heavy.", &SimParams::heavy_tower},
}};

struct TowerIntKey {
    const char* name;
    int TowerParams::* field;
};
constexpr std::array<TowerIntKey, 5> kTowerInts = {{
    {"range_px", &TowerParams::range_px},
    {"damage", &TowerParams::damage},
    {"fire_interval_ms", &TowerParams::fire_interval_ms},
    {"shot_speed_px_s", &TowerParams::shot_speed_px_s},
    {"health", &TowerParams::health},
}};

constexpr const char* kDelayKey = "routes.delay_ticks";

void line(std::string& out, std::string_view key, int value) {
    out.append(key).append(" = ").append(std::to_string(value)).append("\n");
}

} // namespace

std::string to_tuning(const SimParams& p) {
    std::string out = "# Pixel TD live tuning: the arena reloads this file when it changes.\n";
    out += "# Every number is a (start) value; see design/test-arena.md.\n";
    for (const IntKey& k : kInts) {
        if (k.section != nullptr) {
            out.append("\n# ").append(k.section).append("\n");
        }
        line(out, k.key, p.*(k.field));
    }
    out += "\n# Test towers (sight = range)\n";
    for (const TowerKey& t : kTowers) {
        for (const TowerIntKey& k : kTowerInts) {
            line(out, std::string(t.prefix) + k.name, (p.*(t.field)).*(k.field));
        }
    }
    out += "\n# Routes: ticks from a change until the rebuilt routes are used (0: at once)\n";
    line(out, kDelayKey, p.route_delay_ticks);
    return out;
}

bool from_tuning(std::string_view text, SimParams& p, std::string& errors) {
    const std::size_t errors_before = errors.size();
    KeyValueLines lines(text);
    while (lines.next()) {
        if (lines.bad()) {
            report_line(errors, lines.line(), "expected key = value");
            continue;
        }
        const std::string_view key = lines.key();
        int* field = nullptr;
        for (const IntKey& k : kInts) {
            if (key == k.key) {
                field = &(p.*(k.field));
            }
        }
        for (const TowerKey& t : kTowers) {
            const std::string_view prefix = t.prefix;
            if (key.size() > prefix.size() && key.substr(0, prefix.size()) == prefix) {
                for (const TowerIntKey& k : kTowerInts) {
                    if (key.substr(prefix.size()) == k.name) {
                        field = &((p.*(t.field)).*(k.field));
                    }
                }
            }
        }
        if (key == kDelayKey) {
            field = &p.route_delay_ticks;
        }
        if (field == nullptr) {
            report_line(errors, lines.line(),
                        std::string("unknown key '") + std::string(key) + "'");
        } else if (!parse_int(lines.value(), *field)) {
            report_line(errors, lines.line(),
                        std::string("bad value for '") + std::string(key) + "': '" +
                            std::string(lines.value()) + "'");
        }
    }
    const int clamped = validate(p);
    if (clamped > 0) {
        errors.append(std::to_string(clamped)).append(" value(s) were out of range and clamped\n");
    }
    return errors.size() == errors_before;
}

} // namespace pixeltd
