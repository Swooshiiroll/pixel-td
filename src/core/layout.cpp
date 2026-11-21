#include "core/layout.hpp"

#include <cinttypes>
#include <cstdio>
#include <string>

#include "core/preset.hpp"
#include "core/text.hpp"

namespace pixeltd {
namespace {

constexpr std::string_view kMapPrefix = "map.";

const char* kind_name(BlockKind k) noexcept {
    return k == BlockKind::Light ? "light" : "heavy";
}

// "light 1155,600" or "heavy 1200,640 first": a kind, a subcell and (towers) a targeting.
bool parse_piece(std::string_view value, bool armed, Piece& out) {
    std::string_view rest = trim(value);
    const auto sp = rest.find(' ');
    if (sp == std::string_view::npos) {
        return false;
    }
    const std::string_view kind = rest.substr(0, sp);
    if (kind == "light") {
        out.kind = BlockKind::Light;
    } else if (kind == "heavy") {
        out.kind = BlockKind::Heavy;
    } else {
        return false;
    }
    rest = trim(rest.substr(sp + 1));
    const auto sp2 = rest.find(' ');
    const std::string_view at = rest.substr(0, sp2);
    const std::string_view tail =
        sp2 == std::string_view::npos ? std::string_view{} : trim(rest.substr(sp2 + 1));
    const auto comma = at.find(',');
    if (comma == std::string_view::npos || !parse_int(at.substr(0, comma), out.top_left.x) ||
        !parse_int(at.substr(comma + 1), out.top_left.y)) {
        return false;
    }
    out.armed = armed;
    out.targeting = Targeting::First;
    if (!armed) {
        return tail.empty();
    }
    if (tail.empty()) {
        return true;
    }
    for (int t = 0; t < kTargetings; ++t) {
        if (tail == targeting_name(static_cast<Targeting>(t))) {
            out.targeting = static_cast<Targeting>(t);
            return true;
        }
    }
    return false;
}

} // namespace

std::string to_layout(const Layout& l) {
    std::string out = "# Pixel TD arena layout: the map, and the blocks and towers on it.\n";
    char seed[32];
    std::snprintf(seed, sizeof seed, "0x%" PRIx64, l.seed);
    out.append("seed = ").append(seed).append("\n\n# Blocks and test towers (subcell X,Y)\n");
    for (const Piece& p : l.pieces) {
        out.append(p.armed ? "tower = " : "block = ")
            .append(kind_name(p.kind))
            .append(" ")
            .append(std::to_string(p.top_left.x))
            .append(",")
            .append(std::to_string(p.top_left.y));
        if (p.armed) {
            out.append(" ").append(targeting_name(p.targeting));
        }
        out.append("\n");
    }
    out.append("\n# Map generator settings\n");
    // The preset's lines, each key prefixed with "map." (comments and blank lines dropped).
    const std::string preset = to_preset(l.gen);
    std::string_view rest = preset;
    while (!rest.empty()) {
        const auto nl = rest.find('\n');
        const std::string_view ln = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        if (!trim(ln).empty() && trim(ln)[0] != '#') {
            out.append(kMapPrefix).append(ln).append("\n");
        }
    }
    return out;
}

bool from_layout(std::string_view text, Layout& l, std::string& errors) {
    const std::size_t errors_before = errors.size();
    l.pieces.clear();
    std::string preset; // the map. lines, with the prefix taken off, for from_preset
    KeyValueLines lines(text);
    while (lines.next()) {
        if (lines.bad()) {
            report_line(errors, lines.line(), "expected key = value");
            continue;
        }
        const std::string_view key = lines.key();
        const std::string_view value = lines.value();
        bool ok = true;
        if (key == "seed") {
            ok = parse_u64(value, l.seed);
        } else if (key == "block" || key == "tower") {
            Piece p;
            ok = parse_piece(value, key == "tower", p);
            if (ok) {
                l.pieces.push_back(p);
            }
        } else if (key.size() > kMapPrefix.size() &&
                   key.substr(0, kMapPrefix.size()) == kMapPrefix) {
            preset.append(key.substr(kMapPrefix.size())).append(" = ").append(value).append("\n");
        } else {
            report_line(errors, lines.line(),
                        std::string("unknown key '") + std::string(key) + "'");
            continue;
        }
        if (!ok) {
            report_line(errors, lines.line(),
                        std::string("bad value for '") + std::string(key) + "': '" +
                            std::string(value) + "'");
        }
    }
    std::string preset_errors;
    if (!from_preset(preset, l.gen, preset_errors)) {
        errors.append("map settings: ").append(preset_errors);
    }
    return errors.size() == errors_before;
}

std::vector<Piece> pieces_of(const Sim& sim) {
    std::vector<Piece> out;
    for (const Block& b : sim.blocks()) {
        if (b.alive) {
            out.push_back(Piece{b.top_left, b.kind, b.armed, b.gun.targeting});
        }
    }
    return out;
}

int place_pieces(Sim& sim, const std::vector<Piece>& pieces) {
    int refused = 0;
    for (const Piece& p : pieces) {
        const Place r =
            p.armed ? sim.place_tower(p.top_left, p.kind) : sim.place_block(p.top_left, p.kind);
        if (r != Place::Ok) {
            ++refused;
            continue;
        }
        if (p.armed) {
            sim.set_targeting(sim.block_at(p.top_left), p.targeting);
        }
    }
    return refused;
}

} // namespace pixeltd
