#include "core/preset.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "core/text.hpp"

namespace pixeltd {
namespace {

constexpr std::array<const char*, kCurves> kCurveNames = {"linear", "smoothstep", "quintic"};
constexpr std::array<const char*, kShapes> kShapeNames = {"normal", "ridged", "billowed"};
constexpr std::array<const char*, kBases> kBasisNames = {"value", "simplex"};

// A plain int value in GenParams.
struct IntKey {
    const char* key;
    int GenParams::* field;
};

// The noise layers, by preset prefix.
struct NoiseKey {
    const char* prefix;
    NoiseSpec GenParams::* field;
};

// The int values inside a NoiseSpec.
struct NoiseIntKey {
    const char* name;
    int NoiseSpec::* field;
};

constexpr std::array<NoiseKey, 8> kNoise = {{
    {"fertility_noise", &GenParams::fertility_noise},
    {"moisture_noise", &GenParams::moisture_noise},
    {"mountain_noise", &GenParams::mountain_noise},
    {"outcrop_noise", &GenParams::outcrop_noise},
    {"cave_noise", &GenParams::cave_noise},
    {"river_noise", &GenParams::river_noise},
    {"lake_rough", &GenParams::lake_rough},
    {"forest_noise", &GenParams::forest_noise},
}};

constexpr std::array<NoiseIntKey, 6> kNoiseInts = {{
    {"step", &NoiseSpec::step},
    {"octaves", &NoiseSpec::octaves},
    {"lacunarity_pct", &NoiseSpec::lacunarity_pct},
    {"persistence_pct", &NoiseSpec::persistence_pct},
    {"warp_cells", &NoiseSpec::warp_cells},
    {"warp_step", &NoiseSpec::warp_step},
}};

// The plain values, in file order, each with the section it starts (or nullptr).
struct Entry {
    const char* section;
    IntKey key;
};
constexpr std::array<Entry, 40> kInts = {{
    {"Mountains", {"mountain_share_pct", &GenParams::mountain_share_pct}},
    {nullptr, {"mountain_edge_bias", &GenParams::mountain_edge_bias}},
    {nullptr, {"mountain_edge_reach_pct", &GenParams::mountain_edge_reach_pct}},
    {nullptr, {"mountain_smooth_passes", &GenParams::mountain_smooth_passes}},
    {nullptr, {"core_clear_cells", &GenParams::core_clear_cells}},
    {nullptr, {"outcrop_share_pct", &GenParams::outcrop_share_pct}},
    {nullptr, {"rock_min_cells", &GenParams::rock_min_cells}},
    {"Caves", {"cave_depth_cells", &GenParams::cave_depth_cells}},
    {nullptr, {"cave_share_pct", &GenParams::cave_share_pct}},
    {nullptr, {"cave_min_cells", &GenParams::cave_min_cells}},
    {"River", {"river_pct", &GenParams::river_pct}},
    {nullptr, {"river_mountain_cost", &GenParams::river_mountain_cost}},
    {nullptr, {"river_deep_width", &GenParams::river_deep_width}},
    {nullptr, {"river_bank_width", &GenParams::river_bank_width}},
    {nullptr, {"fords_min", &GenParams::fords_min}},
    {nullptr, {"fords_max", &GenParams::fords_max}},
    {nullptr, {"ford_length", &GenParams::ford_length}},
    {"Lakes", {"lakes_min", &GenParams::lakes_min}},
    {nullptr, {"lakes_max", &GenParams::lakes_max}},
    {nullptr, {"lake_radius_min", &GenParams::lake_radius_min}},
    {nullptr, {"lake_radius_max", &GenParams::lake_radius_max}},
    {nullptr, {"lake_rim_cells", &GenParams::lake_rim_cells}},
    {"Ground", {"wet_reach_cells", &GenParams::wet_reach_cells}},
    {nullptr, {"wet_moisture_pct", &GenParams::wet_moisture_pct}},
    {nullptr, {"marsh_moisture_pct", &GenParams::marsh_moisture_pct}},
    {nullptr, {"gravel_reach_cells", &GenParams::gravel_reach_cells}},
    {nullptr, {"sand_fertility_pct", &GenParams::sand_fertility_pct}},
    {nullptr, {"rich_fertility_pct", &GenParams::rich_fertility_pct}},
    {"Plants", {"forest_threshold_pct", &GenParams::forest_threshold_pct}},
    {nullptr, {"forest_ramp_pct", &GenParams::forest_ramp_pct}},
    {nullptr, {"tree_rich_pct", &GenParams::tree_rich_pct}},
    {nullptr, {"tree_soil_pct", &GenParams::tree_soil_pct}},
    {nullptr, {"grass_fertility_pct", &GenParams::grass_fertility_pct}},
    {nullptr, {"bush_pct", &GenParams::bush_pct}},
    {"Core", {"core_tries", &GenParams::core_tries}},
    {"Spawns", {"spawn_edge_pct", &GenParams::spawn_edge_pct}},
    {nullptr, {"min_spawn_dist", &GenParams::min_spawn_dist}},
    {nullptr, {"spawn_spacing", &GenParams::spawn_spacing}},
    {"Towers and range", {"light_range_px", &GenParams::light_range_px}},
    {"Retries", {"attempts", &GenParams::attempts}},
}};

int* core_area_field(GenParams& p, std::string_view key) {
    if (key == "core_area.x") {
        return &p.core_area.x;
    }
    if (key == "core_area.y") {
        return &p.core_area.y;
    }
    if (key == "core_area.w") {
        return &p.core_area.w;
    }
    if (key == "core_area.h") {
        return &p.core_area.h;
    }
    return nullptr;
}

template <std::size_t N>
bool parse_name(std::string_view s, const std::array<const char*, N>& names, int& out) {
    const std::string_view t = trim(s);
    for (std::size_t i = 0; i < N; ++i) {
        if (t == names[i]) {
            out = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

void line(std::string& out, std::string_view key, int value) {
    out.append(key).append(" = ").append(std::to_string(value)).append("\n");
}

} // namespace

const char* curve_name(Curve c) noexcept {
    const auto i = static_cast<std::size_t>(c);
    return i < kCurveNames.size() ? kCurveNames[i] : "smoothstep";
}

const char* shape_name(Shape s) noexcept {
    const auto i = static_cast<std::size_t>(s);
    return i < kShapeNames.size() ? kShapeNames[i] : "normal";
}

const char* basis_name(Basis b) noexcept {
    const auto i = static_cast<std::size_t>(b);
    return i < kBasisNames.size() ? kBasisNames[i] : "value";
}

std::string to_preset(const GenParams& p) {
    std::string out = "# Pixel TD map generator preset: settings only (no seed).\n";
    out += "# Curves: linear | smoothstep | quintic. Shapes: normal | ridged | billowed.\n";
    for (const NoiseKey& n : kNoise) {
        const NoiseSpec& spec = p.*(n.field);
        out.append("\n# Noise: ").append(n.prefix).append("\n");
        const std::string prefix = std::string(n.prefix) + ".";
        for (const NoiseIntKey& k : kNoiseInts) {
            line(out, prefix + k.name, spec.*(k.field));
        }
        out.append(prefix).append("curve = ").append(curve_name(spec.curve)).append("\n");
        out.append(prefix).append("shape = ").append(shape_name(spec.shape)).append("\n");
        out.append(prefix).append("basis = ").append(basis_name(spec.basis)).append("\n");
    }
    for (const Entry& e : kInts) {
        if (e.section != nullptr) {
            out.append("\n# ").append(e.section).append("\n");
        }
        if (e.key.field != nullptr) {
            line(out, e.key.key, p.*(e.key.field));
        }
        if (std::string_view(e.key.key) == "core_tries") {
            line(out, "core_area.x", p.core_area.x);
            line(out, "core_area.y", p.core_area.y);
            line(out, "core_area.w", p.core_area.w);
            line(out, "core_area.h", p.core_area.h);
        }
        if (std::string_view(e.key.key) == "spawn_edge_pct") {
            out.append("spawn_waves = ");
            for (std::size_t i = 0; i < p.spawn_waves.size(); ++i) {
                out.append(i > 0 ? ", " : "").append(std::to_string(p.spawn_waves[i]));
            }
            out.append("\n");
        }
    }
    return out;
}

bool from_preset(std::string_view text, GenParams& p, std::string& errors) {
    const std::size_t errors_before = errors.size();
    int line_no = 0;
    const auto report = [&](std::string_view what) {
        errors.append("line ")
            .append(std::to_string(line_no))
            .append(": ")
            .append(what)
            .append("\n");
    };
    while (!text.empty()) {
        ++line_no;
        const auto nl = text.find('\n');
        std::string_view raw = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        const auto hash = raw.find('#');
        const std::string_view body =
            trim(hash == std::string_view::npos ? raw : raw.substr(0, hash));
        if (body.empty()) {
            continue;
        }
        const auto eq = body.find('=');
        if (eq == std::string_view::npos) {
            report("expected key = value");
            continue;
        }
        const std::string_view key = trim(body.substr(0, eq));
        const std::string_view value = trim(body.substr(eq + 1));
        bool known = false;
        bool ok = true;
        // Noise layers: prefix.name
        for (const NoiseKey& n : kNoise) {
            const std::string_view prefix = n.prefix;
            if (key.size() <= prefix.size() + 1 || key.substr(0, prefix.size()) != prefix ||
                key[prefix.size()] != '.') {
                continue;
            }
            const std::string_view name = key.substr(prefix.size() + 1);
            NoiseSpec& spec = p.*(n.field);
            for (const NoiseIntKey& k : kNoiseInts) {
                if (name == k.name) {
                    known = true;
                    ok = parse_int(value, spec.*(k.field));
                }
            }
            int e = 0;
            if (name == "curve") {
                known = true;
                ok = parse_name(value, kCurveNames, e);
                if (ok) {
                    spec.curve = static_cast<Curve>(e);
                }
            } else if (name == "shape") {
                known = true;
                ok = parse_name(value, kShapeNames, e);
                if (ok) {
                    spec.shape = static_cast<Shape>(e);
                }
            } else if (name == "basis") {
                known = true;
                ok = parse_name(value, kBasisNames, e);
                if (ok) {
                    spec.basis = static_cast<Basis>(e);
                }
            }
        }
        for (const Entry& e : kInts) {
            if (e.key.field != nullptr && key == e.key.key) {
                known = true;
                ok = parse_int(value, p.*(e.key.field));
            }
        }
        if (int* area = core_area_field(p, key); area != nullptr) {
            known = true;
            ok = parse_int(value, *area);
        }
        if (key == "spawn_waves") {
            known = true;
            std::vector<int> waves;
            std::string_view rest = value;
            while (ok && !rest.empty()) {
                const auto comma = rest.find(',');
                int w = 0;
                ok = parse_int(rest.substr(0, comma), w);
                waves.push_back(w);
                rest =
                    comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            }
            if (ok && waves.size() == p.spawn_waves.size()) {
                for (std::size_t i = 0; i < waves.size(); ++i) {
                    p.spawn_waves[i] = waves[i];
                }
            } else {
                ok = false;
            }
        }
        if (!known) {
            report(std::string("unknown key '") + std::string(key) + "'");
        } else if (!ok) {
            report(std::string("bad value for '") + std::string(key) + "': '" + std::string(value) +
                   "'");
        }
    }
    const int clamped = validate(p);
    if (clamped > 0) {
        errors.append(std::to_string(clamped)).append(" value(s) were out of range and clamped\n");
    }
    return errors.size() == errors_before;
}

} // namespace pixeltd
