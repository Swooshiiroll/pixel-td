// Pixel TD: SDL3 front end. At whole-number zoom steps the world is drawn 1:1 into a view texture
// of (window size / scale), then shown at that scale: pixels stay square and crisp, and the view
// fills the window. Zoomed out past 1x (1/2 and the whole map) it's drawn straight into a
// window-sized texture, shrunk and smooth: the one exception to pixel-perfect (design/world.md,
// Camera).
//
//   PixelTD [--seed N] [--probe X,Y]... [--overlay NAME] [--zoom STEP] [--no-fog] [--window WxH]
//           [--preset FILE] [--panel] [--map-shot file.bmp] [--map-scale N] [--frames N]
//           [--screenshot file.bmp]
//     --seed N          developer tool: generate the map from seed N (decimal, or 0x hex)
//     --probe X,Y       developer tool: place a probe with its top-left at subcell X,Y (8 px
//                       subgrid; repeatable)
//     --overlay NAME    developer tool: start in an overlay: elevation, octave1, octave2, octave3,
//                       height, mesa, rough or material (issue #13)
//     --zoom STEP       developer tool: start at `whole` (the whole map), `half`, or the
//                       whole-number scale N
//     --no-fog          developer tool: start with the fog hidden (F toggles it)
//     --window WxH      developer tool: open the window at W x H (screenshots)
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

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
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
#include "core/los.hpp"
#include "core/map.hpp"
#include "core/mapgen.hpp"
#include "core/overlay.hpp"
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
        } else if (std::strcmp(argv[i], "--overlay") == 0 && has_value) {
            if (!parse_overlay(argv[++i], out.overlay)) {
                std::fprintf(stderr, "--overlay takes elevation, octave1, octave2, octave3, "
                                     "height, mesa, rough or material\n");
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
            std::fprintf(stderr, "usage: PixelTD [--seed N] [--probe X,Y]... [--overlay NAME] "
                                 "[--zoom whole|half|N] [--no-fog] [--window WxH] [--preset FILE] "
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

    [[nodiscard]] int probe_range_px() const noexcept { return params.light_range_px; }
};

// True if two square subcell footprints overlap.
bool overlap(SubCell a, int a_size, SubCell b, int b_size) {
    return a.x < b.x + b_size && b.x < a.x + a_size && a.y < b.y + b_size && b.y < a.y + a_size;
}

// A probe can go where a light tower could: flat, buildable ground, off the core and other probes.
bool probe_fits(const World& world, SubCell top_left) {
    if (!placement_ok(world.map, top_left, kLightSub, world.params)) {
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
    const Rect lifted = world.fog.explore_from(world.map, centre_of(top_left, kLightSub),
                                               world.probe_range_px(), world.params);
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
    world.stats = game::measure(m, world.params, world.gen_ms, view.bake_ms());
    SDL_Log("map seed 0x%016llx: generated in %.2f ms (attempt %d%s), baked in %.1f ms; core at "
            "subcell %d,%d; %zu spawns",
            static_cast<unsigned long long>(seed), world.gen_ms, m.stats.attempts,
            m.stats.fallback ? ", FALLBACK" : "", view.bake_ms(), m.core ? m.core->x : -1,
            m.core ? m.core->y : -1, m.spawns.size());
    return ok;
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
    if (!m.height.in_bounds(hover)) {
        std::snprintf(out, n, "view: %s", overlay_label(overlay));
        return;
    }
    if (overlay == Overlay::Elevation) {
        std::snprintf(out, n, "view: %s  height %d", overlay_label(overlay), m.height.at(hover));
        return;
    }
    if (!L.valid) {
        std::snprintf(out, n, "view: %s  fallback map: no noise", overlay_label(overlay));
        return;
    }
    int value = 0;
    switch (overlay) {
    case Overlay::Octave1:
        value = L.octave[0].at(hover);
        break;
    case Overlay::Octave2:
        value = L.octave[1].at(hover);
        break;
    case Overlay::Octave3:
        value = L.octave[2].at(hover);
        break;
    case Overlay::Height:
        value = L.height_sum.at(hover);
        break;
    case Overlay::Mesa:
        value = L.mesa_noise.at(hover);
        break;
    case Overlay::Rough:
        value = L.rough.at(hover);
        break;
    default:
        value = L.material.at(hover);
        break;
    }
    std::snprintf(out, n, "view: %s  value %d%s", overlay_label(overlay), value,
                  overlay == Overlay::Mesa && L.mesa.at(hover) != 0 ? " (terraced)" : "");
}

// The HUD, drawn into the current target `width` pixels wide (in HUD pixels).
void draw_hud(SDL_Renderer* r, const World& world, const Screen& s, float width, Cell hover,
              bool probe_mode, Overlay overlay, bool fog_shown) {
    char line[224];
    char ground[64] = "";
    char zoom[32];
    const Map& m = world.map;
    if (m.height.in_bounds(hover)) {
        static constexpr const char* kNames[kMaterials] = {"dust", "gravel", "ash", "rust"};
        const char* what = is_formation(m, hover)                    ? "rock formation"
                           : is_cliff_cell(m, hover, world.params)   ? "cliff"
                           : footprint_ok(m, hover, 1, world.params) ? "buildable"
                                                                     : "";
        std::snprintf(ground, sizeof ground, "h%d %s %s", m.height.at(hover),
                      kNames[static_cast<int>(m.material.at(hover))], what);
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
    const bool second = overlay != Overlay::Off;
    const SDL_FRect strip{0.0f, 0.0f, width, second ? 20.0f : 10.0f};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
    SDL_RenderFillRect(r, &strip);
    SDL_SetRenderDrawColor(r, 170, 236, 210, 255);
    SDL_RenderDebugText(r, 2.0f, 1.0f, line);
    if (second) {
        char view[128];
        describe_overlay(world, overlay, hover, view, sizeof view);
        SDL_SetRenderDrawColor(r, 255, 220, 140, 255);
        SDL_RenderDebugText(r, 2.0f, 11.0f, view);
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
    view.draw(xf, {kMapPxW, kMapPxH}, world.fog);
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
        MapView view(renderer);
        Screen screen;
        const char* base = SDL_GetBasePath();
        game::TuningPanel panel(std::string(base != nullptr ? base : "") + "presets/");
        panel.set_open(opts.panel);
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
        CameraPos cam = centred_on(core_rect(world.map), screen.view);
        bool probe_mode = false;
        bool regen_pending = false; // the tuning panel changed something while dragging
        Uint64 last_regen = 0;
        Cell hover{};
        SubCell probe_at{};

        pixeltd::FixedStep step(kTickNs, kMaxTicksPerFrame);
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
            const int ticks = step.advance(static_cast<std::int64_t>(now - last));
            last = now;
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

            // The world pixel under the cursor, the cell it's in, and in probe mode the probe's
            // top-left subcell so the cursor sits on its centre.
            const float k = screen.k();
            const int wx = cam.x + static_cast<int>(mx * density / k);
            const int wy = cam.y + static_cast<int>(my * density / k);
            hover = Cell{wx / kCellPx, wy / kCellPx};
            const int half = kLightSub * kSubPx / 2;
            probe_at =
                SubCell{(wx - half + kSubPx / 2) / kSubPx, (wy - half + kSubPx / 2) / kSubPx};

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
                const auto change = panel.draw(world.params, seed, world.stats, top * 2.0f);
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
            view.draw(xf, screen.view, world.fog);
            for (const SubCell c : world.probes) {
                view.draw_probe(xf, c, kProbeColour);
            }
            if (probe_mode) {
                view.draw_probe_preview(xf, world.map, world.params, probe_at,
                                        probe_fits(world, probe_at), world.probe_range_px());
            }
            // The HUD at the standard view's text size: scaled up in the window-sized target.
            const int hud = crisp ? 1 : std::max(1, screen.out_h / kStandardViewH);
            SDL_SetRenderScale(renderer, static_cast<float>(hud), static_cast<float>(hud));
            draw_hud(renderer, world, screen,
                     static_cast<float>(crisp ? screen.view.w : screen.out_w / hud), hover,
                     probe_mode, view.overlay(), view.fog_shown());
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
