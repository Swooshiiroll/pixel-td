// Pixel TD: SDL3 front end. At whole-number zoom steps the world is drawn 1:1 into a view texture
// of (window size / scale), then shown at that scale: pixels stay square and crisp, and the view
// fills the window. Zoomed out past 1x (1/2 and the whole map) it's drawn straight into a
// window-sized texture, shrunk and smooth: the one exception to pixel-perfect (design/world.md,
// Camera).
//
//   PixelTD [--seed N] [--probe X,Y]... [--overlay NAME] [--zoom STEP] [--look X,Y] [--no-fog]
//           [--block X,Y]... [--heavy X,Y]... [--send] [--stream] [--routes]
//           [--window WxH] [--preset FILE] [--panel] [--map-shot file.bmp] [--map-scale N]
//           [--frames N] [--screenshot file.bmp]
//     --seed N          developer tool: generate the map from seed N (decimal, or 0x hex)
//     --probe X,Y       developer tool: place a probe with its top-left at subcell X,Y (8 px
//                       subgrid; repeatable)
//     --overlay NAME    developer tool: start in an overlay: mountain, fertility or moisture (issue
//     #13)
//     --zoom STEP       developer tool: start at `whole` (the whole map), `half`, or the
//                       whole-number scale N
//     --look X,Y        developer tool: start with the camera centred on cell X,Y (#35)
//     --no-fog          developer tool: start with the fog hidden (F toggles it)
//     --block X,Y       developer tool: place a light block with its top-left at subcell X,Y
//                       (repeatable; --heavy X,Y for a heavy one) (#11)
//     --send            developer tool: send a walker from every spawn at start (E)
//     --stream          developer tool: start with the walker stream on (Shift+E)
//     --routes          developer tool: start with the route lines shown (R)
//     --tower X,Y       developer tool: place a light test tower at subcell X,Y (repeatable;
//                       --heavy-tower X,Y for a heavy one) (#11 phase 2)
//     --layout FILE     developer tool: start in a saved arena layout (map, blocks, towers)
//     --wave N          developer tool: send a wave of N walkers per spawn at start
//     --speed X         developer tool: start at sim speed X (0.25, 0.5, 1, 2, 4 or 8)
//     --pause-at T      developer tool: pause when the sim reaches tick T
//     --select X,Y      developer tool: select the test tower covering subcell X,Y
//     --window WxH     developer tool: open the window at W x H (screenshots)
//     --preset FILE     developer tool: generate with the settings in a preset file (#21)
//     --panel           developer tool: open the map-tuning panel at start (Tab toggles it)
//     --map-shot F      developer tool: save the whole 15360x8640 map to F (BMP), shrunk by
//                       --map-scale (2, 4 or 8; default 4: 3840x2160), then exit. With no probes
//                       everything is shown explored; with probes, the fog is shown.
//     --frames N        quit after N frames (smoke tests)
//     --screenshot F    with --frames: save the last frame to F (BMP)
//
// Controls: pan with WASD / arrows / the window edges / right-drag; zoom with the mouse wheel or
// + and -, out to the whole map; developer tools: P toggles probe placement and a left-click
// places one (a light tower's sight and reach), O / Shift+O step through the overlays, F hides
// and shows the fog, F5 makes a new random map, Tab opens the map-tuning panel; Esc quits.
// Pathfinding (#11): B cycles build mode (off, light block, heavy block; left-click places,
// right-click removes), E sends a walker from every spawn, Shift+E toggles the stream, R shows
// the routes; hovering a walker shows its stats. The test arena (#11 phase 2): T cycles test
// tower building (off, light, heavy), a left-click on a tower selects it, G cycles the hovered
// tower's targeting, Space pauses, . steps a tick (Shift+. ten), [ and ] change the speed; the
// tuning file (tuning.txt next to the exe) is reloaded when it changes.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/camera.hpp"
#include "core/fixed_step.hpp"
#include "core/fog.hpp"
#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/hash.hpp"
#include "core/layout.hpp"
#include "core/los.hpp"
#include "core/map.hpp"
#include "core/mapgen.hpp"
#include "core/overlay.hpp"
#include "core/sim.hpp"
#include "core/sim_params.hpp"
#include "core/sim_tuning.hpp"
#include "core/terrain.hpp"
#include "game/map_view.hpp"
#include "game/tuning_panel.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <string>

namespace {

using namespace pixeltd;
using pixeltd::game::DrawXf;
using pixeltd::game::MapView;

constexpr std::int64_t kTickNs = 1'000'000'000 / 60;
constexpr int kMaxTicksPerFrame = 8;
constexpr int kPanPerTick = 18;  // window pixels per tick, the same speed at every zoom
constexpr float kEdgePan = 8.0f; // window points from the edge that start edge panning
constexpr SDL_Color kProbeColour{250, 220, 120, 200};
constexpr Uint64 kRegenThrottleNs = 120'000'000; // tuning panel: regenerate at most this often
constexpr Uint64 kTuningCheckNs = 500'000'000;   // the tuning file is checked this often
// The arena's sim speeds (#11 phase 2), as fractions of real time: [ and ] step through them.
constexpr std::array<Zoom, 6> kSpeeds = {{{1, 4}, {1, 2}, {1, 1}, {2, 1}, {4, 1}, {8, 1}}};
constexpr int kNormalSpeed = 2;
constexpr int kMaxSimTicksPerFrame = 64; // the sim's catch-up cap (8x speed needs 8 a frame)

// The --zoom option.
enum class StartZoom : std::uint8_t { Standard, Whole, Half, Scale };

struct Options {
    long frames = 0;
    const char* screenshot = nullptr;
    const char* map_shot = nullptr;
    bool has_seed = false;
    std::uint64_t seed = 0;
    std::vector<SubCell> probes;
    Overlay overlay = Overlay::Off;
    StartZoom zoom = StartZoom::Standard;
    int zoom_scale = 0;
    bool has_look = false;
    Cell look{};
    // Pathfinding (#11): blocks placed at start (top-left subcells), and walkers on their way.
    std::vector<SubCell> light_blocks;
    std::vector<SubCell> heavy_blocks;
    bool send = false;
    bool stream = false;
    bool routes = false;
    // The test arena (#11 phase 2).
    std::vector<SubCell> light_towers;
    std::vector<SubCell> heavy_towers;
    const char* layout = nullptr;
    int wave = 0;
    int speed = 2; // an index into kSpeeds: 1x
    long pause_at = -1;
    bool has_select = false;
    SubCell select{};
    bool no_fog = false;
    const char* preset = nullptr;
    bool panel = false;
    int map_scale = 4; // --map-shot shrinks the map by this (2, 4 or 8)
    int window_w = kStandardViewW * 2;
    int window_h = kStandardViewH * 2;
};

// Parses "A<sep>B" into two whole numbers. False on anything else.
bool parse_pair(const char* text, char sep, int& a, int& b) {
    char* end = nullptr;
    const long x = std::strtol(text, &end, 10);
    if (end == text || *end != sep) {
        return false;
    }
    const char* rest = end + 1;
    const long y = std::strtol(rest, &end, 10);
    if (end == rest || *end != '\0') {
        return false;
    }
    a = static_cast<int>(x);
    b = static_cast<int>(y);
    return true;
}

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--frames") == 0 && has_value) {
            out.frames = std::strtol(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && has_value) {
            out.screenshot = argv[++i];
        } else if (std::strcmp(argv[i], "--map-shot") == 0 && has_value) {
            out.map_shot = argv[++i];
        } else if (std::strcmp(argv[i], "--seed") == 0 && has_value) {
            char* end = nullptr;
            out.seed = std::strtoull(argv[++i], &end, 0);
            out.has_seed = end != nullptr && *end == '\0';
            if (!out.has_seed) {
                std::fprintf(stderr, "--seed takes a number (decimal or 0x hex)\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--probe") == 0 && has_value) {
            SubCell c;
            if (!parse_pair(argv[++i], ',', c.x, c.y)) {
                std::fprintf(stderr, "--probe takes a subcell as X,Y\n");
                return false;
            }
            out.probes.push_back(c);
        } else if ((std::strcmp(argv[i], "--block") == 0 || std::strcmp(argv[i], "--heavy") == 0) &&
                   has_value) {
            const bool heavy = argv[i][2] == 'h';
            SubCell c;
            if (!parse_pair(argv[++i], ',', c.x, c.y)) {
                std::fprintf(stderr, "--block and --heavy take a subcell as X,Y\n");
                return false;
            }
            (heavy ? out.heavy_blocks : out.light_blocks).push_back(c);
        } else if (std::strcmp(argv[i], "--send") == 0) {
            out.send = true;
        } else if (std::strcmp(argv[i], "--stream") == 0) {
            out.stream = true;
        } else if (std::strcmp(argv[i], "--routes") == 0) {
            out.routes = true;
        } else if ((std::strcmp(argv[i], "--tower") == 0 ||
                    std::strcmp(argv[i], "--heavy-tower") == 0) &&
                   has_value) {
            const bool heavy = argv[i][2] == 'h';
            SubCell c;
            if (!parse_pair(argv[++i], ',', c.x, c.y)) {
                std::fprintf(stderr, "--tower and --heavy-tower take a subcell as X,Y\n");
                return false;
            }
            (heavy ? out.heavy_towers : out.light_towers).push_back(c);
        } else if (std::strcmp(argv[i], "--layout") == 0 && has_value) {
            out.layout = argv[++i];
        } else if (std::strcmp(argv[i], "--wave") == 0 && has_value) {
            out.wave = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--speed") == 0 && has_value) {
            const double x = std::strtod(argv[++i], nullptr);
            out.speed = -1;
            for (std::size_t s = 0; s < kSpeeds.size(); ++s) {
                if (std::abs(x - static_cast<double>(kSpeeds[s].num) / kSpeeds[s].den) < 1e-6) {
                    out.speed = static_cast<int>(s);
                }
            }
            if (out.speed < 0) {
                std::fprintf(stderr, "--speed takes 0.25, 0.5, 1, 2, 4 or 8\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--pause-at") == 0 && has_value) {
            out.pause_at = std::strtol(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--select") == 0 && has_value) {
            if (!parse_pair(argv[++i], ',', out.select.x, out.select.y)) {
                std::fprintf(stderr, "--select takes a subcell as X,Y\n");
                return false;
            }
            out.has_select = true;
        } else if (std::strcmp(argv[i], "--overlay") == 0 && has_value) {
            if (!parse_overlay(argv[++i], out.overlay)) {
                std::fprintf(stderr, "--overlay takes mountain, fertility or moisture\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--zoom") == 0 && has_value) {
            const char* z = argv[++i];
            char* end = nullptr;
            if (std::strcmp(z, "whole") == 0) {
                out.zoom = StartZoom::Whole;
            } else if (std::strcmp(z, "half") == 0) {
                out.zoom = StartZoom::Half;
            } else if (const long k = std::strtol(z, &end, 10); end != z && *end == '\0' && k > 0) {
                out.zoom = StartZoom::Scale;
                out.zoom_scale = static_cast<int>(k);
            } else {
                std::fprintf(stderr, "--zoom takes whole, half or a whole-number scale\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--look") == 0 && has_value) {
            if (!parse_pair(argv[++i], ',', out.look.x, out.look.y) || out.look.x < 0 ||
                out.look.y < 0 || out.look.x >= kMapW || out.look.y >= kMapH) {
                std::fprintf(stderr, "--look takes a cell on the map as X,Y\n");
                return false;
            }
            out.has_look = true;
        } else if (std::strcmp(argv[i], "--no-fog") == 0) {
            out.no_fog = true;
        } else if (std::strcmp(argv[i], "--preset") == 0 && has_value) {
            out.preset = argv[++i];
        } else if (std::strcmp(argv[i], "--panel") == 0) {
            out.panel = true;
        } else if (std::strcmp(argv[i], "--map-scale") == 0 && has_value) {
            out.map_scale = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
            if (out.map_scale != 2 && out.map_scale != 4 && out.map_scale != 8) {
                std::fprintf(stderr, "--map-scale takes 2, 4 or 8\n");
                return false;
            }
        } else if (std::strcmp(argv[i], "--window") == 0 && has_value) {
            if (!parse_pair(argv[++i], 'x', out.window_w, out.window_h) || out.window_w < 1 ||
                out.window_h < 1) {
                std::fprintf(stderr, "--window takes a size as WxH\n");
                return false;
            }
        } else {
            std::fprintf(
                stderr,
                "usage: PixelTD [--seed N] [--probe X,Y]... [--overlay NAME] "
                "[--zoom whole|half|N] [--look X,Y] [--no-fog] [--block X,Y]... "
                "[--heavy X,Y]... [--send] [--stream] [--routes] [--tower X,Y]... "
                "[--heavy-tower X,Y]... [--layout FILE] [--wave N] [--speed X] [--pause-at T] "
                "[--select X,Y] [--window WxH] [--preset FILE] "
                "[--panel] [--map-shot file.bmp] [--map-scale 2|4|8] [--frames N] "
                "[--screenshot file.bmp]\n");
            return false;
        }
    }
    return true;
}

// Normal runs get a fresh random seed; the simulation itself never reads the clock.
std::uint64_t random_seed() {
    return splitmix64(SDL_GetPerformanceCounter() ^ (SDL_GetTicksNS() << 17u));
}

struct World {
    Map map;
    GenParams params;
    GenLayers layers; // the noise behind the map, for the overlays
    Fog fog;
    std::vector<SubCell> probes; // top-left subcells of placed probes (light-tower sized)
    double gen_ms = 0.0;
    game::MapStats stats; // for the tuning panel
    // Pathfinding and mazing, phase 1 (#11): walkers, blocks and the core's health. Started over
    // with every new map; its numbers carry over.
    Sim sim;
    SimParams sim_params;
    double tick_ms = 0.0; // the sim's average tick time (a running average)

    [[nodiscard]] int probe_range_px() const noexcept { return params.light_range_px; }
};

// The sim's clock for its route-build timing readout (diagnostics only; the sim never reads it).
std::int64_t now_ns() {
    return static_cast<std::int64_t>(SDL_GetTicksNS());
}

// True if two square subcell footprints overlap.
bool overlap(SubCell a, int a_size, SubCell b, int b_size) {
    return a.x < b.x + b_size && b.x < a.x + a_size && a.y < b.y + b_size && b.y < a.y + a_size;
}

// True if any subcell of a size x size footprint is under a block.
bool on_block(const World& world, SubCell top_left, int size) {
    for (int y = top_left.y; y < top_left.y + size; ++y) {
        for (int x = top_left.x; x < top_left.x + size; ++x) {
            if (world.sim.block_at({x, y}) >= 0) {
                return true;
            }
        }
    }
    return false;
}

// A probe can go where a light tower could: on buildable ground, off the core, blocks and other
// probes.
bool probe_fits(const World& world, SubCell top_left) {
    if (!placement_ok(world.map, top_left, kLightSub) || on_block(world, top_left, kLightSub)) {
        return false;
    }
    if (world.map.core && overlap(top_left, kLightSub, *world.map.core, kCoreSub)) {
        return false;
    }
    return std::none_of(world.probes.begin(), world.probes.end(),
                        [&](SubCell c) { return overlap(top_left, kLightSub, c, kLightSub); });
}

// Places a probe and lifts the fog it sees. False if it doesn't fit there.
bool place_probe(World& world, MapView& view, SubCell top_left) {
    if (!probe_fits(world, top_left)) {
        return false;
    }
    world.probes.push_back(top_left);
    const Rect lifted =
        world.fog.explore_from(world.map, centre_of(top_left, kLightSub), world.probe_range_px());
    view.update_fog(world.fog, lifted);
    return true;
}

// Generates the map for `seed` with the world's settings and colours it.
bool new_map(World& world, MapView& view, std::uint64_t seed) {
    const Uint64 start = SDL_GetTicksNS();
    world.map = generate_map(seed, world.params, &world.layers);
    world.gen_ms = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    world.probes.clear();
    world.fog.reset();
    world.fog.explore_core(world.map, world.params.light_range_px);
    const Map& m = world.map;
    const bool ok = view.set_map(m, world.layers, world.fog);
    world.stats = game::measure(m, world.gen_ms, view.bake_ms());
    world.sim.reset(m, world.sim_params);
    SDL_Log("map seed 0x%016llx: generated in %.2f ms (attempt %d%s), baked in %.1f ms; core at "
            "subcell %d,%d; %zu spawns",
            static_cast<unsigned long long>(seed), world.gen_ms, m.stats.attempts,
            m.stats.fallback ? ", FALLBACK" : "", view.bake_ms(), m.core ? m.core->x : -1,
            m.core ? m.core->y : -1, m.spawns.size());
    for (const Spawn& s : m.spawns) {
        SDL_Log("  spawn at cell %d,%d (wave %d)", s.cell.x, s.cell.y, s.opens_at_wave);
    }
    return ok;
}

// Build mode (#11): off, or placing light or heavy blocks, or light or heavy test towers.
enum class BuildMode : std::uint8_t { Off, Light, Heavy, LightTower, HeavyTower };

[[nodiscard]] BlockKind build_kind(BuildMode m) noexcept {
    return m == BuildMode::Heavy || m == BuildMode::HeavyTower ? BlockKind::Heavy
                                                               : BlockKind::Light;
}
[[nodiscard]] bool build_armed(BuildMode m) noexcept {
    return m == BuildMode::LightTower || m == BuildMode::HeavyTower;
}

// The block a build-mode click places: its top-left subcell, so the cursor sits on its centre.
SubCell block_under(int wx, int wy, BlockKind kind) {
    const int half = block_size_sub(kind) * kSubPx / 2;
    return SubCell{(wx - half + kSubPx / 2) / kSubPx, (wy - half + kSubPx / 2) / kSubPx};
}

// Why a block can't go at `top_left` (the sim's rules, and not on a probe), or nullptr if it can.
const char* block_refusal(const World& world, SubCell top_left, BlockKind kind) {
    const int size = block_size_sub(kind);
    for (const SubCell p : world.probes) {
        if (top_left.x < p.x + kLightSub && p.x < top_left.x + size &&
            top_left.y < p.y + kLightSub && p.y < top_left.y + size) {
            return "on a probe";
        }
    }
    const Place p = world.sim.can_place(top_left, kind);
    return p == Place::Ok ? nullptr : place_name(p);
}

// Places a block or a test tower; the reason when it can't go there, or nullptr. A tower lifts
// the fog it can see, like a probe.
const char* place_piece(World& world, MapView& view, SubCell top_left, BlockKind kind, bool armed) {
    const char* why = block_refusal(world, top_left, kind);
    if (why != nullptr) {
        return why;
    }
    if (!armed) {
        (void)world.sim.place_block(top_left, kind);
        return nullptr;
    }
    (void)world.sim.place_tower(top_left, kind);
    const Rect lifted = world.fog.explore_from(world.map, centre_of(top_left, block_size_sub(kind)),
                                               world.sim.tower_params(kind).range_px);
    view.update_fog(world.fog, lifted);
    return nullptr;
}

// The armed block (test tower) covering subcell s, or -1.
int tower_at(const World& world, SubCell s) {
    const int slot = world.sim.block_at(s);
    return slot >= 0 && world.sim.blocks()[static_cast<std::size_t>(slot)].armed ? slot : -1;
}

// The live-tuning file (#11 phase 2): every SimParams value, next to the exe.
struct TuningFile {
    std::string path;
    SDL_Time modified = 0;
    Uint64 last_check = 0;
};

// Reads the tuning file over the defaults (so the file is the whole truth) and applies it.
void load_tuning(World& world, TuningFile& tf, std::string& message) {
    std::size_t size = 0;
    void* data = SDL_LoadFile(tf.path.c_str(), &size);
    if (data == nullptr) {
        message = std::string("can't read the tuning file: ") + SDL_GetError();
        return;
    }
    const std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    SimParams p;
    std::string errors;
    const bool ok = from_tuning(text, p, errors);
    world.sim_params = p;
    world.sim.set_params(p);
    message = ok ? "tuning file loaded" : "tuning file loaded with problems:\n" + errors;
    SDL_Log("%s", message.c_str());
}

bool save_tuning(const World& world, TuningFile& tf, std::string& message) {
    const std::string text = to_tuning(world.sim_params);
    const bool ok = SDL_SaveFile(tf.path.c_str(), text.data(), text.size());
    message = ok ? "saved the tuning file" : std::string("save failed: ") + SDL_GetError();
    SDL_PathInfo info;
    if (ok && SDL_GetPathInfo(tf.path.c_str(), &info)) {
        tf.modified = info.modify_time; // our own write isn't a change to reload
    }
    return ok;
}

// Reloads the tuning file when it has changed on disk (checked every 500 ms).
void watch_tuning(World& world, TuningFile& tf, std::string& message) {
    const Uint64 now = SDL_GetTicksNS();
    if (now - tf.last_check < kTuningCheckNs) {
        return;
    }
    tf.last_check = now;
    SDL_PathInfo info;
    if (SDL_GetPathInfo(tf.path.c_str(), &info) && info.modify_time != tf.modified) {
        tf.modified = info.modify_time;
        load_tuning(world, tf, message);
    }
}

// Saves the current map, blocks and towers as a layout.
bool save_layout(const World& world, const std::string& path, std::string& message) {
    Layout l;
    l.seed = world.map.seed;
    l.gen = world.params;
    l.pieces = pieces_of(world.sim);
    const std::string text = to_layout(l);
    const bool ok = SDL_SaveFile(path.c_str(), text.data(), text.size());
    message = ok ? "saved " + path : std::string("save failed: ") + SDL_GetError();
    return ok;
}

// Loads a layout: its map, then its pieces (each tower lifting its fog). False if the map
// couldn't be made.
bool load_layout(World& world, MapView& view, const std::string& path, std::string& message) {
    std::size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        message = "can't read " + path + ": " + SDL_GetError();
        return true;
    }
    const std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    Layout l;
    std::string errors;
    const bool clean = from_layout(text, l, errors);
    world.params = l.gen;
    if (!new_map(world, view, l.seed)) {
        return false;
    }
    int refused = 0;
    for (const Piece& p : l.pieces) {
        if (place_piece(world, view, p.top_left, p.kind, p.armed) != nullptr) {
            ++refused;
        } else if (p.armed) {
            world.sim.set_targeting(world.sim.block_at(p.top_left), p.targeting);
        }
    }
    world.sim.update_routes();
    message = "loaded " + path;
    if (refused > 0) {
        message += " (" + std::to_string(refused) + " pieces no longer fit)";
    }
    if (!clean) {
        message += "\nproblems:\n" + errors;
    }
    SDL_Log("%s", message.c_str());
    return true;
}

bool save_target(SDL_Renderer* r, const char* path) {
    SDL_Surface* shot = SDL_RenderReadPixels(r, nullptr);
    if (shot == nullptr) {
        SDL_Log("reading pixels failed: %s", SDL_GetError());
        return false;
    }
    const bool ok = SDL_SaveBMP(shot, path);
    if (!ok) {
        SDL_Log("saving %s failed: %s", path, SDL_GetError());
    }
    SDL_DestroySurface(shot);
    return ok;
}

// The window's view: its zoom steps and current step, and the render target. At whole-number
// steps the target is the view's size (shown scaled up, crisp); at fractional steps it's the
// window's size (the world is drawn shrunk into it). Recreated whenever the window's pixel size
// or the zoom changes.
struct Screen {
    int out_w = 0;
    int out_h = 0;
    ZoomSteps steps{};
    int offset = 0; // steps out from the standard zoom (negative = closer), kept across resizes
    int index = 0;  // the current step
    Zoom zoom{};
    ViewSize view{};
    SDL_Texture* target = nullptr;
    int target_w = 0;
    int target_h = 0;

    [[nodiscard]] float k() const noexcept {
        return static_cast<float>(zoom.num) / static_cast<float>(zoom.den);
    }
};

bool refresh_screen(SDL_Renderer* r, Screen& s) {
    int w = 0;
    int h = 0;
    SDL_GetRenderOutputSize(r, &w, &h);
    s.out_w = w;
    s.out_h = h;
    s.steps = zoom_steps(w, h);
    s.index = std::clamp(s.steps.standard + s.offset, 0, s.steps.count - 1);
    s.offset = s.index - s.steps.standard;
    s.zoom = s.steps.steps[static_cast<std::size_t>(s.index)];
    s.view = view_size(w, h, s.zoom);
    const bool crisp = pixel_perfect(s.zoom);
    const int tw = crisp ? s.view.w : w;
    const int th = crisp ? s.view.h : h;
    if (s.target != nullptr && tw == s.target_w && th == s.target_h) {
        return true;
    }
    if (s.target != nullptr) {
        SDL_DestroyTexture(s.target);
    }
    s.target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tw, th);
    if (s.target == nullptr) {
        SDL_Log("view target %dx%d failed: %s", tw, th, SDL_GetError());
        return false;
    }
    s.target_w = tw;
    s.target_h = th;
    SDL_SetTextureScaleMode(s.target, SDL_SCALEMODE_NEAREST);
    return true;
}

// Sets the starting zoom step from --zoom (after the first refresh_screen).
void apply_start_zoom(SDL_Renderer* r, Screen& s, StartZoom start, int scale) {
    int index = s.steps.standard;
    for (int i = 0; i < s.steps.count; ++i) {
        const Zoom z = s.steps.steps[static_cast<std::size_t>(i)];
        if ((start == StartZoom::Half && z == Zoom{1, 2}) ||
            (start == StartZoom::Scale && z == Zoom{scale, 1})) {
            index = i;
        }
    }
    if (start == StartZoom::Whole || (start == StartZoom::Half && index == s.steps.standard)) {
        index = s.steps.count - 1; // no separate 1/2 step: the whole map is already that far out
    }
    s.offset = index - s.steps.standard;
    refresh_screen(r, s);
}

// Centres the view on a pixel rectangle.
CameraPos centred_on(const RectPx& area, ViewSize view) {
    const int cx = area.x + area.w / 2;
    const int cy = area.y + area.h / 2;
    return clamp_camera({cx - view.w / 2, cy - view.h / 2}, view);
}

// The core's footprint (or the map's centre if there's no core).
RectPx core_rect(const Map& map) {
    return map.core ? rect_of(*map.core, kCoreSub) : RectPx{kMapPxW / 2, kMapPxH / 2, 1, 1};
}

// Steps the zoom by `delta` (positive = closer), keeping the world point under (px, py) in place.
void zoom_by(SDL_Renderer* r, Screen& s, CameraPos& cam, int delta, int px, int py) {
    const int index = std::clamp(s.index - delta, 0, s.steps.count - 1);
    if (index == s.index) {
        return;
    }
    const Zoom from = s.zoom;
    s.offset = index - s.steps.standard;
    if (!refresh_screen(r, s)) {
        return;
    }
    cam = clamp_camera(zoom_about(cam, from, s.zoom, px, py), s.view);
}

// The overlay's value under the cursor, for the HUD's second line.
void describe_overlay(const World& world, Overlay overlay, Cell hover, char* out, std::size_t n) {
    const GenLayers& L = world.layers;
    const Map& m = world.map;
    if (!m.rock.in_bounds(hover)) {
        std::snprintf(out, n, "view: %s", overlay_label(overlay));
        return;
    }
    if (!L.valid) {
        std::snprintf(out, n, "view: %s  fallback map: no noise", overlay_label(overlay));
        return;
    }
    const int value = overlay == Overlay::Mountain    ? L.mountain.at(hover)
                      : overlay == Overlay::Fertility ? L.fertility.at(hover)
                                                      : L.moisture.at(hover);
    std::snprintf(out, n, "view: %s  value %d", overlay_label(overlay), value);
}

// The HUD, drawn into the current target `width` pixels wide (in HUD pixels).
// What the HUD shows of the arena's time and the hovered test tower (#11 phase 2).
struct HudArena {
    int speed = kNormalSpeed;
    bool paused = false;
    int hover_tower = -1;
};

void draw_hud(SDL_Renderer* r, const World& world, const Screen& s, float width, Cell hover,
              bool probe_mode, Overlay overlay, bool fog_shown, BuildMode build,
              const char* refusal, int hover_walker, const HudArena& arena) {
    char line[224];
    char ground[64] = "";
    char zoom[32];
    const Map& m = world.map;
    if (m.rock.in_bounds(hover)) {
        static constexpr const char* kNames[kGrounds] = {
            "soil", "rich soil", "sand", "gravel", "mud", "marsh", "shallow water", "deep water"};
        static constexpr const char* kPlantNames[kPlants] = {"", " grass", " bush", " tree"};
        const char* what = is_rock(m, hover) ? "rock" : is_buildable(m, hover) ? "buildable" : "";
        std::snprintf(ground, sizeof ground, "%s%s %s",
                      kNames[static_cast<int>(m.ground.at(hover))],
                      kPlantNames[static_cast<int>(m.plant.at(hover))], what);
    }
    if (s.zoom.den == 1) {
        std::snprintf(zoom, sizeof zoom, "%dx", s.zoom.num);
    } else if (s.index == s.steps.count - 1) {
        std::snprintf(zoom, sizeof zoom, "whole %.3fx", static_cast<double>(s.k()));
    } else {
        std::snprintf(zoom, sizeof zoom, "%d/%d", s.zoom.num, s.zoom.den);
    }
    // The relative resolution: the world pixels on screen, and their share of the map.
    char area[48];
    const ViewSize shown = visible_area(s.view);
    const int share = map_share_bp(s.view); // hundredths of a percent
    if (share >= 10000) {
        std::snprintf(area, sizeof area, "%dx%d (100%%)", shown.w, shown.h);
    } else {
        std::snprintf(area, sizeof area, "%dx%d (%d.%02d%%)", shown.w, shown.h, share / 100,
                      share % 100);
    }
    std::snprintf(line, sizeof line, "seed %llx  zoom %s %s  gen %.1fms  cell %d,%d %s%s%s",
                  static_cast<unsigned long long>(m.seed), zoom, area, world.gen_ms, hover.x,
                  hover.y, ground, fog_shown ? "" : "  [fog off]",
                  probe_mode ? "  [PROBE: click to place]" : "");
    // The sim (#11): the core, the walkers, the stream, the time and build mode.
    const Sim& sim = world.sim;
    char sim_line[224];
    const char* mode =
        build == BuildMode::Light   ? "  [BUILD light block: click places, right-click removes]"
        : build == BuildMode::Heavy ? "  [BUILD heavy block: click places, right-click removes]"
        : build == BuildMode::LightTower
            ? "  [BUILD light tower: click places, right-click removes]"
        : build == BuildMode::HeavyTower
            ? "  [BUILD heavy tower: click places, right-click removes]"
            : "";
    char core[48];
    if (sim.core_fallen()) {
        std::snprintf(core, sizeof core, "CORE FALLEN");
    } else {
        std::snprintf(core, sizeof core, "core %d/%d", sim.core_health(), sim.params().core_health);
    }
    char time[32];
    const Zoom sp = kSpeeds[static_cast<std::size_t>(arena.speed)];
    if (arena.paused) {
        std::snprintf(time, sizeof time, "PAUSED");
    } else if (sp.den == 1) {
        std::snprintf(time, sizeof time, "x%d", sp.num);
    } else {
        std::snprintf(time, sizeof time, "x1/%d", sp.den);
    }
    std::snprintf(sim_line, sizeof sim_line, "%s  %s  tick %lld  walkers %zu  stream %s%s%s%s",
                  core, time, static_cast<long long>(sim.ticks()), sim.walkers().size(),
                  sim.stream() ? "on" : "off", mode, refusal != nullptr ? "  can't: " : "",
                  refusal != nullptr ? refusal : "");
    // The latest wave, and how the sim is keeping up.
    char arena_line[224];
    char wave[96] = "no waves yet";
    if (!sim.waves().empty()) {
        const WaveStats& ws = sim.waves().back();
        const std::int64_t end = ws.done() ? ws.end_tick : sim.ticks();
        std::snprintf(
            wave, sizeof wave, "wave #%u %d/%d sent, %d killed, %d leaked, core -%d, %.1fs%s",
            ws.id, ws.sent, ws.planned, ws.killed, ws.leaked, ws.core_damage,
            static_cast<double>(end - ws.start_tick) / kTicksPerSecond, ws.done() ? "" : "...");
    }
    int towers = 0;
    for (const Block& b : sim.blocks()) {
        towers += b.alive && b.armed ? 1 : 0;
    }
    std::snprintf(arena_line, sizeof arena_line,
                  "%s  |  tick %.3fms  route %.0fms%s  waits %d  shots %d  towers %d", wave,
                  world.tick_ms, static_cast<double>(sim.last_build_ns()) / 1e6,
                  sim.routes_pending() ? " (pending)" : "", sim.route_waits(),
                  sim.projectiles_in_flight(), towers);
    // A hovered walker's stats, or a hovered tower's.
    char walker_line[160] = "";
    if (hover_walker >= 0) {
        const Walker& w = sim.walkers()[static_cast<std::size_t>(hover_walker)];
        const char* state = w.state == WalkerState::Breaking  ? "breaking a block"
                            : w.state == WalkerState::Stuck   ? "stuck"
                            : w.state == WalkerState::Waiting ? "waiting for new routes"
                                                              : "walking";
        std::snprintf(walker_line, sizeof walker_line,
                      "walker #%u (wave %u)  speed %d px/s (%d%%)  health %d/%d  %s%s", w.id,
                      w.wave,
                      sim.params().walker_speed_px_s * w.speed_scale_pct / 100 * w.speed_pct / 100,
                      w.speed_pct, w.health, w.max_health, state, w.sealed ? " (sealed in)" : "");
    } else if (arena.hover_tower >= 0) {
        const Block& b = sim.blocks()[static_cast<std::size_t>(arena.hover_tower)];
        const TowerParams& tp = sim.tower_params(b.kind);
        std::snprintf(walker_line, sizeof walker_line,
                      "%s tower  range %d  dmg/shot %d every %dms  health %d/%d  dps %d  dmg %lld  "
                      "kills %d  %s (G)",
                      b.kind == BlockKind::Light ? "light" : "heavy", tp.range_px, tp.damage,
                      tp.fire_interval_ms, b.health(), b.max_health,
                      sim.tower_dps(arena.hover_tower), static_cast<long long>(b.gun.damage),
                      b.gun.kills, targeting_name(b.gun.targeting));
    }

    const bool overlay_line = overlay != Overlay::Off;
    const int lines = 3 + (overlay_line ? 1 : 0) + (walker_line[0] != '\0' ? 1 : 0);
    const SDL_FRect strip{0.0f, 0.0f, width, 10.0f * static_cast<float>(lines)};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
    SDL_RenderFillRect(r, &strip);
    SDL_SetRenderDrawColor(r, 170, 236, 210, 255);
    SDL_RenderDebugText(r, 2.0f, 1.0f, line);
    float y = 11.0f;
    if (overlay_line) {
        char view[128];
        describe_overlay(world, overlay, hover, view, sizeof view);
        SDL_SetRenderDrawColor(r, 255, 220, 140, 255);
        SDL_RenderDebugText(r, 2.0f, y, view);
        y += 10.0f;
    }
    if (sim.core_fallen()) {
        SDL_SetRenderDrawColor(r, 255, 110, 90, 255);
    } else {
        SDL_SetRenderDrawColor(r, 200, 210, 255, 255);
    }
    SDL_RenderDebugText(r, 2.0f, y, sim_line);
    y += 10.0f;
    SDL_SetRenderDrawColor(r, 170, 255, 235, 255);
    SDL_RenderDebugText(r, 2.0f, y, arena_line);
    y += 10.0f;
    if (walker_line[0] != '\0') {
        SDL_SetRenderDrawColor(r, 230, 200, 255, 255);
        SDL_RenderDebugText(r, 2.0f, y, walker_line);
    }
}

// Developer tool: renders the whole map once, shrunk by `scale` (2, 4 or 8), and saves it.
int run_map_shot(SDL_Renderer* r, std::uint64_t seed, const std::vector<SubCell>& probes,
                 Overlay overlay, bool no_fog, const GenParams& params, int scale,
                 const char* path) {
    World world;
    world.params = params;
    MapView view(r);
    view.set_overlay(overlay, world.map, world.layers, world.fog); // applied by the first bake
    view.set_fog_shown(!no_fog);
    if (!new_map(world, view, seed)) {
        return 1;
    }
    if (probes.empty()) {
        world.fog.explore_all();
        view.update_fog(world.fog, Rect{0, 0, kMapW, kMapH});
    }
    for (const SubCell c : probes) {
        if (!place_probe(world, view, c)) {
            SDL_Log("probe at subcell %d,%d doesn't fit (not flat buildable ground, or "
                    "overlapping)",
                    c.x, c.y);
        }
    }
    SDL_Texture* full = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                          kMapPxW / scale, kMapPxH / scale);
    if (full == nullptr) {
        SDL_Log("map target failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderTarget(r, full);
    const DrawXf xf{CameraPos{0, 0}, 1.0f / static_cast<float>(scale), true};
    view.draw(xf, {kMapPxW, kMapPxH}, world.fog, world.sim);
    for (const SubCell c : world.probes) {
        view.draw_probe(xf, c, kProbeColour);
    }
    const bool ok = save_target(r, path);
    SDL_SetRenderTarget(r, nullptr);
    SDL_DestroyTexture(full);
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parse_options(argc, argv, opts)) {
        return 2;
    }

    SDL_SetAppMetadata("Pixel TD", PIXELTD_VERSION, "io.github.swooshiiroll.pixeltd");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    const SDL_WindowFlags flags = opts.map_shot != nullptr
                                      ? SDL_WINDOW_HIDDEN
                                      : SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Pixel TD", opts.window_w, opts.window_h, flags, &window,
                                     &renderer)) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    const char* backend = SDL_GetRendererName(renderer);
    SDL_Log("Pixel TD %s, SDL %d.%d.%d, renderer: %s", PIXELTD_VERSION,
            SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
            SDL_VERSIONNUM_MICRO(SDL_GetVersion()), backend);

    const std::uint64_t first_seed = opts.has_seed ? opts.seed : random_seed();
    GenParams start_params;
    if (opts.preset != nullptr) {
        std::string errors;
        if (!game::load_preset_file(opts.preset, start_params, errors)) {
            SDL_Log("preset %s: %s", opts.preset, errors.c_str());
        }
    }
    if (opts.map_shot != nullptr) {
        const int code = run_map_shot(renderer, first_seed, opts.probes, opts.overlay, opts.no_fog,
                                      start_params, opts.map_scale, opts.map_shot);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return code;
    }

    // Dear ImGui for the map-tuning panel (#23): drawn at window resolution over the game.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // no imgui.ini next to the game
    ImGui::StyleColorsDark();
    {
        int out_w = 0;
        int out_h = 0;
        SDL_GetRenderOutputSize(renderer, &out_w, &out_h);
        const float ui = std::max(1.0f, static_cast<float>(out_h) / 720.0f);
        ImGui::GetStyle().ScaleAllSizes(ui);
        ImGui::GetStyle().FontScaleDpi = ui;
    }
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    int exit_code = 0;
    {
        World world;
        world.params = start_params;
        world.sim.set_clock(&now_ns);
        MapView view(renderer);
        Screen screen;
        const char* base_path = SDL_GetBasePath();
        const std::string base = base_path != nullptr ? base_path : "";
        const std::string layouts_dir = base + "layouts/";
        game::TuningPanel panel(base + "presets/", layouts_dir);
        panel.set_open(opts.panel);
        // The live-tuning file (#11 phase 2): loaded if it's there, written with the defaults if
        // not, then watched.
        game::ArenaUi arena;
        TuningFile tuning;
        tuning.path = base + "tuning.txt";
        arena.tuning_path = tuning.path;
        {
            SDL_PathInfo info;
            if (SDL_GetPathInfo(tuning.path.c_str(), &info)) {
                tuning.modified = info.modify_time;
                load_tuning(world, tuning, arena.tuning_message);
            } else {
                (void)save_tuning(world, tuning, arena.tuning_message);
                arena.tuning_message = "wrote the defaults to the tuning file";
            }
            tuning.last_check = SDL_GetTicksNS();
        }
        view.set_overlay(opts.overlay, world.map, world.layers, world.fog); // before the bake
        view.set_fog_shown(!opts.no_fog);
        if (!new_map(world, view, first_seed) || !refresh_screen(renderer, screen)) {
            exit_code = 1;
        }
        if (opts.zoom != StartZoom::Standard) {
            apply_start_zoom(renderer, screen, opts.zoom, opts.zoom_scale);
        }
        for (const SubCell c : opts.probes) {
            if (!place_probe(world, view, c)) {
                SDL_Log("probe at subcell %d,%d doesn't fit (not flat buildable ground, or "
                        "overlapping)",
                        c.x, c.y);
            }
        }
        // A layout, then blocks, towers and walkers from the command line (#11).
        if (opts.layout != nullptr && exit_code == 0 &&
            !load_layout(world, view, opts.layout, arena.layout_message)) {
            exit_code = 1;
        }
        struct StartPieces {
            const std::vector<SubCell>* list;
            BlockKind kind;
            bool armed;
        };
        for (const StartPieces& sp : {StartPieces{&opts.light_blocks, BlockKind::Light, false},
                                      StartPieces{&opts.heavy_blocks, BlockKind::Heavy, false},
                                      StartPieces{&opts.light_towers, BlockKind::Light, true},
                                      StartPieces{&opts.heavy_towers, BlockKind::Heavy, true}}) {
            for (const SubCell c : *sp.list) {
                if (const char* why = place_piece(world, view, c, sp.kind, sp.armed)) {
                    SDL_Log("%s at subcell %d,%d: %s", sp.armed ? "tower" : "block", c.x, c.y, why);
                }
            }
        }
        if (opts.send) {
            world.sim.send_walkers();
        }
        if (opts.wave > 0) {
            WaveSpec w = arena.wave;
            w.count = opts.wave;
            world.sim.send_wave(w);
        }
        world.sim.set_stream(opts.stream);
        world.sim.update_routes();
        if (opts.has_select) {
            arena.selected = tower_at(world, opts.select);
        }
        CameraPos cam = centred_on(
            opts.has_look ? RectPx{opts.look.x * kCellPx, opts.look.y * kCellPx, kCellPx, kCellPx}
                          : core_rect(world.map),
            screen.view);
        bool probe_mode = false;
        bool regen_pending = false; // the tuning panel changed something while dragging
        Uint64 last_regen = 0;
        Cell hover{};
        SubCell probe_at{};
        // Pathfinding (#11): build mode, what it would place, the route lines, the hovered walker.
        BuildMode build = BuildMode::Off;
        SubCell build_at{};
        bool routes_shown = opts.routes;
        int hover_walker = -1;
        float right_down_x = 0.0f; // where the right button went down: a click removes a block,
        float right_down_y = 0.0f; // a drag pans
        // The arena's time (#11 phase 2): the sim runs on its own clock, scaled by the speed.
        int speed = opts.speed;
        bool paused = false;
        int steps_due = 0; // single steps asked for (. and Shift+.)
        int hover_tower = -1;

        pixeltd::FixedStep step(kTickNs, kMaxTicksPerFrame); // the camera's (real time)
        pixeltd::FixedStep sim_step(kTickNs, kMaxSimTicksPerFrame);
        Uint64 last = SDL_GetTicksNS();
        float drag_x = 0.0f;
        float drag_y = 0.0f;
        float wheel = 0.0f;
        long frame = 0;
        bool running = exit_code == 0;

        while (running) {
            const float density = SDL_GetWindowPixelDensity(window);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                // While the panel is using the mouse or keyboard, the game doesn't see them.
                const ImGuiIO& io = ImGui::GetIO();
                const bool mouse_event =
                    e.type == SDL_EVENT_MOUSE_WHEEL || e.type == SDL_EVENT_MOUSE_MOTION ||
                    e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP;
                if (panel.open() && ((mouse_event && io.WantCaptureMouse) ||
                                     (e.type == SDL_EVENT_KEY_DOWN && io.WantCaptureKeyboard &&
                                      e.key.key != SDLK_ESCAPE))) {
                    continue;
                }
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_TAB) {
                    panel.toggle();
                    continue;
                }
                switch (e.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_ESCAPE) {
                        running = false;
                    } else if (e.key.key == SDLK_F5) {
                        if (!new_map(world, view, random_seed())) {
                            running = false;
                            exit_code = 1;
                        }
                        cam = centred_on(core_rect(world.map), screen.view);
                    } else if (e.key.key == SDLK_P) {
                        probe_mode = !probe_mode;
                        if (probe_mode) {
                            build = BuildMode::Off;
                        }
                    } else if (e.key.key == SDLK_B) {
                        build = build == BuildMode::Light   ? BuildMode::Heavy
                                : build == BuildMode::Heavy ? BuildMode::Off
                                                            : BuildMode::Light;
                        if (build != BuildMode::Off) {
                            probe_mode = false;
                        }
                    } else if (e.key.key == SDLK_T) {
                        build = build == BuildMode::LightTower   ? BuildMode::HeavyTower
                                : build == BuildMode::HeavyTower ? BuildMode::Off
                                                                 : BuildMode::LightTower;
                        if (build != BuildMode::Off) {
                            probe_mode = false;
                        }
                    } else if (e.key.key == SDLK_G) {
                        if (hover_tower >= 0) {
                            const Block& b =
                                world.sim.blocks()[static_cast<std::size_t>(hover_tower)];
                            world.sim.set_targeting(
                                hover_tower,
                                static_cast<Targeting>((static_cast<int>(b.gun.targeting) + 1) %
                                                       kTargetings));
                        }
                    } else if (e.key.key == SDLK_SPACE) {
                        paused = !paused;
                    } else if (e.key.key == SDLK_PERIOD) {
                        paused = true;
                        steps_due += (e.key.mod & SDL_KMOD_SHIFT) != 0 ? 10 : 1;
                    } else if (e.key.key == SDLK_LEFTBRACKET) {
                        speed = std::max(0, speed - 1);
                    } else if (e.key.key == SDLK_RIGHTBRACKET) {
                        speed = std::min(static_cast<int>(kSpeeds.size()) - 1, speed + 1);
                    } else if (e.key.key == SDLK_E) {
                        if ((e.key.mod & SDL_KMOD_SHIFT) != 0) {
                            world.sim.set_stream(!world.sim.stream());
                        } else {
                            world.sim.send_walkers();
                        }
                    } else if (e.key.key == SDLK_R) {
                        routes_shown = !routes_shown;
                    } else if (e.key.key == SDLK_F) {
                        view.set_fog_shown(!view.fog_shown());
                    } else if (e.key.key == SDLK_O) {
                        const int dir = (e.key.mod & SDL_KMOD_SHIFT) != 0 ? -1 : 1;
                        view.set_overlay(step_overlay(view.overlay(), dir), world.map, world.layers,
                                         world.fog);
                        SDL_Log("overlay: %s (baked in %.1f ms)", overlay_label(view.overlay()),
                                view.bake_ms());
                    } else if (e.key.key == SDLK_EQUALS || e.key.key == SDLK_KP_PLUS) {
                        zoom_by(renderer, screen, cam, 1, screen.out_w / 2, screen.out_h / 2);
                    } else if (e.key.key == SDLK_MINUS || e.key.key == SDLK_KP_MINUS) {
                        zoom_by(renderer, screen, cam, -1, screen.out_w / 2, screen.out_h / 2);
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL: {
                    const float dir = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                    wheel += e.wheel.y * dir;
                    const int px = static_cast<int>(e.wheel.mouse_x * density);
                    const int py = static_cast<int>(e.wheel.mouse_y * density);
                    while (wheel >= 1.0f) {
                        zoom_by(renderer, screen, cam, 1, px, py);
                        wheel -= 1.0f;
                    }
                    while (wheel <= -1.0f) {
                        zoom_by(renderer, screen, cam, -1, px, py);
                        wheel += 1.0f;
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION:
                    if ((e.motion.state & SDL_BUTTON_RMASK) != 0) {
                        // Right-drag: the world follows the mouse, in whole world pixels.
                        drag_x += e.motion.xrel * density;
                        drag_y += e.motion.yrel * density;
                        const float k = screen.k();
                        const int dx = static_cast<int>(drag_x / k);
                        const int dy = static_cast<int>(drag_y / k);
                        drag_x -= static_cast<float>(dx) * k;
                        drag_y -= static_cast<float>(dy) * k;
                        cam = clamp_camera({cam.x - dx, cam.y - dy}, screen.view);
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (probe_mode && e.button.button == SDL_BUTTON_LEFT) {
                        place_probe(world, view, probe_at);
                    } else if (build != BuildMode::Off && e.button.button == SDL_BUTTON_LEFT) {
                        (void)place_piece(world, view, build_at, build_kind(build),
                                          build_armed(build));
                    } else if (e.button.button == SDL_BUTTON_LEFT) {
                        arena.selected = hover_tower; // a tower, or nothing
                    } else if (e.button.button == SDL_BUTTON_RIGHT) {
                        right_down_x = e.button.x;
                        right_down_y = e.button.y;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    // In build mode, a right click (not a drag) removes the block under it.
                    if (build != BuildMode::Off && e.button.button == SDL_BUTTON_RIGHT &&
                        std::abs(e.button.x - right_down_x) < 4.0f &&
                        std::abs(e.button.y - right_down_y) < 4.0f) {
                        const float k = screen.k();
                        const int wx = cam.x + static_cast<int>(e.button.x * density / k);
                        const int wy = cam.y + static_cast<int>(e.button.y * density / k);
                        world.sim.remove_block_at({wx / kSubPx, wy / kSubPx});
                    }
                    break;
                default:
                    break;
                }
            }

            if (!refresh_screen(renderer, screen)) {
                exit_code = 1;
                break;
            }
            cam = clamp_camera(cam, screen.view);

            float mx = 0.0f;
            float my = 0.0f;
            SDL_GetMouseState(&mx, &my);
            int win_w = 0;
            int win_h = 0;
            SDL_GetWindowSize(window, &win_w, &win_h);
            const ImGuiIO& ui_io = ImGui::GetIO();
            const bool ui_mouse = panel.open() && ui_io.WantCaptureMouse;
            const bool ui_keys = panel.open() && ui_io.WantCaptureKeyboard;
            const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0 &&
                                 SDL_GetMouseFocus() == window && !ui_mouse;

            const Uint64 now = SDL_GetTicksNS();
            const auto frame_ns = static_cast<std::int64_t>(now - last);
            const int ticks = step.advance(frame_ns);
            last = now;
            // The sim's ticks: its clock runs at the chosen speed, or not at all while paused
            // (single steps still run).
            const Zoom sp = kSpeeds[static_cast<std::size_t>(speed)];
            int sim_ticks = paused ? 0 : sim_step.advance(frame_ns * sp.num / sp.den);
            sim_ticks += steps_due;
            steps_due = 0;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            // The same speed on screen at every zoom.
            const int pan = std::max(1, kPanPerTick * screen.zoom.den / screen.zoom.num);
            for (int i = 0; i < ticks; ++i) {
                int dx = 0;
                int dy = 0;
                if (ui_keys) {
                    // Typing in the panel: no keyboard panning.
                } else if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
                    dx -= pan;
                }
                if (!ui_keys && (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])) {
                    dx += pan;
                }
                if (!ui_keys && (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])) {
                    dy -= pan;
                }
                if (!ui_keys && (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])) {
                    dy += pan;
                }
                if (focused) {
                    if (mx < kEdgePan) {
                        dx -= pan;
                    } else if (mx > static_cast<float>(win_w) - kEdgePan) {
                        dx += pan;
                    }
                    if (my < kEdgePan) {
                        dy -= pan;
                    } else if (my > static_cast<float>(win_h) - kEdgePan) {
                        dy += pan;
                    }
                }
                cam = clamp_camera({cam.x + dx, cam.y + dy}, screen.view);
            }
            // The walkers, blocks, towers and core (#11); routes rebuild in the background.
            for (int i = 0; i < sim_ticks; ++i) {
                if (opts.pause_at >= 0 && world.sim.ticks() >= opts.pause_at) {
                    paused = true;
                    opts.pause_at = -1;
                    break;
                }
                const Uint64 t0 = SDL_GetTicksNS();
                world.sim.tick();
                const double ms = static_cast<double>(SDL_GetTicksNS() - t0) / 1e6;
                world.tick_ms = world.tick_ms * 0.98 + ms * 0.02;
            }
            watch_tuning(world, tuning, arena.tuning_message);

            // The world pixel under the cursor, the cell it's in, and in probe mode the probe's
            // top-left subcell so the cursor sits on its centre.
            const float k = screen.k();
            const int wx = cam.x + static_cast<int>(mx * density / k);
            const int wy = cam.y + static_cast<int>(my * density / k);
            hover = Cell{wx / kCellPx, wy / kCellPx};
            const int half = kLightSub * kSubPx / 2;
            probe_at =
                SubCell{(wx - half + kSubPx / 2) / kSubPx, (wy - half + kSubPx / 2) / kSubPx};
            const BlockKind kind_now = build_kind(build);
            build_at = block_under(wx, wy, kind_now);
            // The test tower under the cursor; the selection is dropped if its tower is gone.
            hover_tower = tower_at(world, SubCell{wx / kSubPx, wy / kSubPx});
            if (arena.selected >= 0) {
                const auto sel = static_cast<std::size_t>(arena.selected);
                if (sel >= world.sim.blocks().size() || !world.sim.blocks()[sel].alive ||
                    !world.sim.blocks()[sel].armed) {
                    arena.selected = -1;
                }
            }
            // The walker under the cursor, if it can be seen there.
            hover_walker = world.sim.walker_at({wx, wy});
            if (hover_walker >= 0 && view.fog_shown() && world.map.core) {
                const PointPx c =
                    world.sim.walkers()[static_cast<std::size_t>(hover_walker)].centre();
                if (!world.fog.explored({c.x / kCellPx, c.y / kCellPx})) {
                    hover_walker = -1;
                }
            }

            // The tuning panel: a frame of UI, then regenerate if it changed something.
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            {
                std::uint64_t seed = world.map.seed;
                const float top =
                    10.0f * static_cast<float>(pixel_perfect(screen.zoom)
                                                   ? screen.zoom.num
                                                   : std::max(1, screen.out_h / kStandardViewH));
                arena.sim = &world.sim;
                const auto change = panel.draw(world.params, seed, world.stats, top * 3.0f,
                                               world.sim_params, arena);
                if (panel.sim_changed()) {
                    world.sim.set_params(world.sim_params); // no new map: walkers carry on
                }
                // What the arena's part of the panel asked for (#11 phase 2).
                if (arena.send_wave) {
                    world.sim.send_wave(arena.wave);
                    arena.send_wave = false;
                }
                if (arena.save_tuning) {
                    (void)save_tuning(world, tuning, arena.tuning_message);
                    arena.save_tuning = false;
                }
                if (arena.targeting >= 0) {
                    world.sim.set_targeting(arena.selected,
                                            static_cast<Targeting>(arena.targeting));
                    arena.targeting = -1;
                }
                if (!arena.save_layout.empty()) {
                    SDL_CreateDirectory(layouts_dir.c_str());
                    (void)save_layout(world, layouts_dir + arena.save_layout + ".txt",
                                      arena.layout_message);
                    arena.save_layout.clear();
                }
                if (!arena.load_layout.empty()) {
                    if (!load_layout(world, view, layouts_dir + arena.load_layout + ".txt",
                                     arena.layout_message)) {
                        exit_code = 1;
                        running = false;
                    }
                    seed = world.map.seed; // the layout's map, not a regenerate
                    cam = centred_on(core_rect(world.map), screen.view);
                    arena.load_layout.clear();
                }
                // A set value regenerates now; a slider being dragged at most every 120 ms (a
                // map takes about 150 ms to generate).
                regen_pending = regen_pending || change == game::TuningPanel::Change::Preview;
                const Uint64 since = SDL_GetTicksNS() - last_regen;
                if (change == game::TuningPanel::Change::Commit ||
                    (regen_pending && since >= kRegenThrottleNs)) {
                    regen_pending = false;
                    last_regen = SDL_GetTicksNS();
                    if (!new_map(world, view, seed)) {
                        exit_code = 1;
                        running = false;
                    }
                }
            }
            ImGui::Render();

            // Whole-number steps: draw 1:1 into the view target. Fractional: shrunk into the
            // window-sized target.
            const bool crisp = pixel_perfect(screen.zoom);
            const DrawXf xf{cam, crisp ? 1.0f : k, !crisp};
            SDL_SetRenderTarget(renderer, screen.target);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            view.draw(xf, screen.view, world.fog, world.sim);
            for (const SubCell c : world.probes) {
                view.draw_probe(xf, c, kProbeColour);
            }
            if (routes_shown) {
                view.draw_routes(xf, world.fog, world.sim);
            }
            if (probe_mode) {
                view.draw_probe_preview(xf, world.map, probe_at, probe_fits(world, probe_at),
                                        world.probe_range_px());
            }
            // The selected test tower's reach and readout, and the hovered one's.
            view.draw_tower_info(xf, world.map, world.sim, arena.selected, true);
            if (hover_tower != arena.selected) {
                view.draw_tower_info(xf, world.map, world.sim, hover_tower, false);
            }
            const char* refusal = nullptr;
            if (build != BuildMode::Off) {
                refusal = block_refusal(world, build_at, kind_now);
                view.draw_block_preview(xf, build_at, kind_now, refusal == nullptr);
            }
            // The HUD at the standard view's text size: scaled up in the window-sized target.
            const int hud = crisp ? 1 : std::max(1, screen.out_h / kStandardViewH);
            SDL_SetRenderScale(renderer, static_cast<float>(hud), static_cast<float>(hud));
            draw_hud(renderer, world, screen,
                     static_cast<float>(crisp ? screen.view.w : screen.out_w / hud), hover,
                     probe_mode, view.overlay(), view.fog_shown(), build, refusal, hover_walker,
                     HudArena{speed, paused, hover_tower});
            SDL_SetRenderScale(renderer, 1.0f, 1.0f);

            ++frame;
            const bool last_frame = opts.frames > 0 && frame >= opts.frames;
            // With the panel open, the screenshot is taken from the window below (with the panel).
            if (last_frame && opts.screenshot != nullptr && !panel.open() &&
                !save_target(renderer, opts.screenshot)) {
                exit_code = 1;
            }

            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            const SDL_FRect dst =
                crisp ? SDL_FRect{0.0f, 0.0f, static_cast<float>(screen.view.w * screen.zoom.num),
                                  static_cast<float>(screen.view.h * screen.zoom.num)}
                      : SDL_FRect{0.0f, 0.0f, static_cast<float>(screen.target_w),
                                  static_cast<float>(screen.target_h)};
            SDL_RenderTexture(renderer, screen.target, nullptr, &dst);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            if (last_frame && opts.screenshot != nullptr && panel.open() &&
                !save_target(renderer, opts.screenshot)) {
                exit_code = 1;
            }
            SDL_RenderPresent(renderer);

            if (last_frame) {
                running = false;
            }
        }
        if (screen.target != nullptr) {
            SDL_DestroyTexture(screen.target);
        }
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
