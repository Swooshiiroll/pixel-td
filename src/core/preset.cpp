#include "core/preset.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace pixeltd {
namespace {

constexpr std::array<const char*, kCurves> kCurveNames = {"linear", "smoothstep", "quintic"};
constexpr std::array<const char*, kShapes> kShapeNames = {"normal", "ridged", "billowed"};
constexpr std::array<const char*, kBases> kBasisNames = {"value", "simplex"};
constexpr std::array<const char*, kTerrainAlgos> kTerrainNames = {"value", "simplex", "eroded"};

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

constexpr std::array<NoiseKey, 5> kNoise = {{
    {"height", &GenParams::height},
    {"natural_height", &GenParams::natural_height},
    {"mesa_noise", &GenParams::mesa_noise},
    {"formation_rough", &GenParams::formation_rough},
    {"material_noise", &GenParams::material_noise},
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
constexpr std::array<Entry, 38> kInts = {{
    {"Height", {"ground_max", &GenParams::ground_max}},
    {"Erosion (the eroded terrain)", {"erosion_droplets", &GenParams::erosion_droplets}},
    {nullptr, {"erosion_lifetime", &GenParams::erosion_lifetime}},
    {nullptr, {"erosion_inertia_pct", &GenParams::erosion_inertia_pct}},
    {nullptr, {"erosion_capacity_pct", &GenParams::erosion_capacity_pct}},
    {nullptr, {"erosion_min_slope_pct", &GenParams::erosion_min_slope_pct}},
    {nullptr, {"erosion_erode_pct", &GenParams::erosion_erode_pct}},
    {nullptr, {"erosion_deposit_pct", &GenParams::erosion_deposit_pct}},
    {nullptr, {"erosion_evaporate_pct", &GenParams::erosion_evaporate_pct}},
    {nullptr, {"erosion_gravity_pct", &GenParams::erosion_gravity_pct}},
    {nullptr, {"erosion_radius", &GenParams::erosion_radius}},
    {"Mesas", {"mesa_share_pct", &GenParams::mesa_share_pct}},
    {nullptr, {"mesa_band", &GenParams::mesa_band}},
    {nullptr, {"mesa_feather_cells", &GenParams::mesa_feather_cells}},
    {nullptr, {"mesa_relief", &GenParams::mesa_relief}},
    {"Cliffs and building", {"cliff_step", &GenParams::cliff_step}},
    {nullptr, {"cliff_share_min_pct", &GenParams::cliff_share_min_pct}},
    {nullptr, {"cliff_share_max_pct", &GenParams::cliff_share_max_pct}},
    {nullptr, {"natural_cliff_share_min_pct", &GenParams::natural_cliff_share_min_pct}},
    {nullptr, {"flat_limit", &GenParams::flat_limit}},
    {"Rock formations", {"formations_min", &GenParams::formations_min}},
    {nullptr, {"formations_max", &GenParams::formations_max}},
    {nullptr, {"formation_radius_min", &GenParams::formation_radius_min}},
    {nullptr, {"formation_radius_max", &GenParams::formation_radius_max}},
    {nullptr, {"formation_scale_min_pct", &GenParams::formation_scale_min_pct}},
    {nullptr, {"formation_scale_max_pct", &GenParams::formation_scale_max_pct}},
    {nullptr, {"formation_height_min", &GenParams::formation_height_min}},
    {nullptr, {"formation_share_max_pct", &GenParams::formation_share_max_pct}},
    {"Core", {"core_tries", &GenParams::core_tries}},
    {"Spawns", {"spawn_edge_pct", &GenParams::spawn_edge_pct}},
    {nullptr, {"min_spawn_dist", &GenParams::min_spawn_dist}},
    {nullptr, {"spawn_spacing", &GenParams::spawn_spacing}},
    {"Towers, sight and range", {"light_range_px", &GenParams::light_range_px}},
    {nullptr, {"eye_height", &GenParams::eye_height}},
    {nullptr, {"target_height", &GenParams::target_height}},
    {nullptr, {"range_pct_per_grade", &GenParams::range_pct_per_grade}},
    {nullptr, {"range_slope_max_pct", &GenParams::range_slope_max_pct}},
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

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r");
    return s.substr(first, last - first + 1);
}

// A whole decimal integer (optionally signed), nothing else.
bool parse_int(std::string_view s, int& out) {
    const std::string str(trim(s));
    if (str.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(str.c_str(), &end, 10);
    if (end == str.c_str() || *end != '\0' || errno != 0 || v < -1000000000L || v > 1000000000L) {
        return false;
    }
    out = static_cast<int>(v);
    return true;
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

const char* terrain_name(TerrainAlgo t) noexcept {
    const auto i = static_cast<std::size_t>(t);
    return i < kTerrainNames.size() ? kTerrainNames[i] : "value";
}

std::string to_preset(const GenParams& p) {
    std::string out = "# Pixel TD map generator preset: settings only (no seed).\n";
    out += "# Curves: linear | smoothstep | quintic. Shapes: normal | ridged | billowed.\n";
    out += "# Bases: value | simplex. Terrains: value | simplex | eroded.\n";
    out.append("\nterrain = ").append(terrain_name(p.terrain)).append("\n");
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
        if (key == "terrain") {
            known = true;
            int t = 0;
            ok = parse_name(value, kTerrainNames, t);
            if (ok) {
                p.terrain = static_cast<TerrainAlgo>(t);
            }
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
