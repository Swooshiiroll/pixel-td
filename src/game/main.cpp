// Pixel TD: SDL3 front end. The world is drawn 1:1 into a view texture of (window size / scale),
// where the scale is a whole number, then shown at that scale: pixels stay square and crisp at
// every zoom step, and the view always fills the window (design/world.md, Camera).
//
//   PixelTD [--seed N] [--probe X,Y]... [--overlay NAME] [--map-shot file.bmp] [--frames N]
//           [--screenshot file.bmp]
//     --seed N          developer tool: generate the map from seed N (decimal, or 0x hex)
//     --probe X,Y       developer tool: place a probe with its top-left at cell X,Y (repeatable)
//     --overlay NAME    developer tool: start in an overlay: elevation, octave1, octave2, octave3,
//                       height, mesa, rough or material (issue #13)
//     --map-shot F      developer tool: save the whole 3840x2160 map to F (BMP), then exit. With
//                       no probes everything is shown explored; with probes, the fog is shown.
//     --frames N        quit after N frames (smoke tests)
//     --screenshot F    with --frames: save the last view frame to F (BMP)
//
// Controls: pan with WASD / arrows / the window edges / right-drag; zoom with the mouse wheel or
// + and -; P toggles probe placement and a left-click places one (developer tools: a probe is a
// light tower's sight and reach, to try out line of sight and fog); O / Shift+O step through the
// overlays (elevation and the generator's noise layers); F5 makes a new random map (developer
// tools); Esc quits.

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

namespace {

using namespace pixeltd;
using pixeltd::game::MapView;

constexpr std::int64_t kTickNs = 1'000'000'000 / 60;
constexpr int kMaxTicksPerFrame = 8;
constexpr int kPanPerTick = 18;  // window pixels per tick, the same speed at every zoom
constexpr float kEdgePan = 8.0f; // window points from the edge that start edge panning
constexpr SDL_Color kProbeColour{250, 220, 120, 200};

struct Options {
    long frames = 0;
    const char* screenshot = nullptr;
    const char* map_shot = nullptr;
    bool has_seed = false;
    std::uint64_t seed = 0;
    std::vector<Cell> probes;
    Overlay overlay = Overlay::Off;
};

// Parses "X,Y" into a cell. False on anything else.
bool parse_cell(const char* text, Cell& out) {
    char* end = nullptr;
    const long x = std::strtol(text, &end, 10);
    if (end == text || *end != ',') {
        return false;
    }
    const char* rest = end + 1;
    const long y = std::strtol(rest, &end, 10);
    if (end == rest || *end != '\0') {
        return false;
    }
    out = Cell{static_cast<int>(x), static_cast<int>(y)};
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
            Cell c;
            if (!parse_cell(argv[++i], c)) {
                std::fprintf(stderr, "--probe takes a cell as X,Y\n");
                return false;
            }
            out.probes.push_back(c);
        } else if (std::strcmp(argv[i], "--overlay") == 0 && has_value) {
            if (!parse_overlay(argv[++i], out.overlay)) {
                std::fprintf(stderr, "--overlay takes elevation, octave1, octave2, octave3, "
                                     "height, mesa, rough or material\n");
                return false;
            }
        } else {
            std::fprintf(stderr, "usage: PixelTD [--seed N] [--probe X,Y]... [--overlay NAME] "
                                 "[--map-shot file.bmp] [--frames N] [--screenshot file.bmp]\n");
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
    std::vector<Cell> probes; // top-left cells of placed probes (2x2, like a light tower)
    double gen_ms = 0.0;

    [[nodiscard]] int probe_range_px() const noexcept { return params.light_range_cells * kCellPx; }
};

// A probe can go where a light tower could: flat, buildable ground, off the core and other probes.
bool probe_fits(const World& world, Cell top_left) {
    if (!footprint_ok(world.map, top_left, kLightSize, world.params)) {
        return false;
    }
    const Rect probe{top_left.x, top_left.y, kLightSize, kLightSize};
    const auto overlaps = [&](Cell c, int size) {
        return probe.x < c.x + size && c.x < probe.x + probe.w && probe.y < c.y + size &&
               c.y < probe.y + probe.h;
    };
    if (world.map.core && overlaps(*world.map.core, kCoreSize)) {
        return false;
    }
    return std::none_of(world.probes.begin(), world.probes.end(),
                        [&](Cell c) { return overlaps(c, kLightSize); });
}

// Places a probe and lifts the fog it sees. False if it doesn't fit there.
bool place_probe(World& world, MapView& view, Cell top_left) {
    if (!probe_fits(world, top_left)) {
        return false;
    }
    world.probes.push_back(top_left);
    const Rect lifted = world.fog.explore_from(world.map, centre_of(top_left, kLightSize),
                                               world.probe_range_px(), world.params);
    view.update(world.map, world.fog, lifted);
    return true;
}

bool new_map(World& world, MapView& view, std::uint64_t seed) {
    const Uint64 start = SDL_GetTicksNS();
    world.map = generate_map(seed, world.params, &world.layers);
    world.gen_ms = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    world.probes.clear();
    world.fog.reset();
    world.fog.explore_core(world.map, world.params.light_range_cells);
    const Map& m = world.map;
    const bool ok = view.set_map(m, world.layers, world.fog);
    SDL_Log("map seed 0x%016llx: generated in %.2f ms (attempt %d%s), baked in %.1f ms; core at "
            "%d,%d; %zu spawns",
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

// The window's view: its whole-number scale and the render target sized to match. Recreated
// whenever the window's pixel size or the zoom changes.
struct Screen {
    int out_w = 0;
    int out_h = 0;
    ZoomLimits limits{};
    int zoom_offset = 0; // steps from the standard zoom, kept across window resizes
    int scale = 1;
    ViewSize view{};
    SDL_Texture* target = nullptr;
};

bool refresh_screen(SDL_Renderer* r, Screen& s) {
    int w = 0;
    int h = 0;
    SDL_GetRenderOutputSize(r, &w, &h);
    const ZoomLimits limits = zoom_limits(w, h);
    const int scale = std::clamp(limits.standard + s.zoom_offset, limits.farthest, limits.closest);
    const ViewSize view = view_size(w, h, scale);
    if (s.target != nullptr && w == s.out_w && h == s.out_h && scale == s.scale) {
        return true;
    }
    s.out_w = w;
    s.out_h = h;
    s.limits = limits;
    s.scale = scale;
    s.zoom_offset = scale - limits.standard;
    s.view = view;
    if (s.target != nullptr) {
        SDL_DestroyTexture(s.target);
    }
    s.target =
        SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, view.w, view.h);
    if (s.target == nullptr) {
        SDL_Log("view target %dx%d failed: %s", view.w, view.h, SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(s.target, SDL_SCALEMODE_NEAREST);
    return true;
}

CameraPos centred_on(const Rect& area, ViewSize view) {
    const int cx = (area.x * 2 + area.w) * kCellPx / 2;
    const int cy = (area.y * 2 + area.h) * kCellPx / 2;
    return clamp_camera({cx - view.w / 2, cy - view.h / 2}, view);
}

// The core's cells (or the map's centre if there's no core).
Rect core_rect(const Map& map) {
    return map.core ? Rect{map.core->x, map.core->y, kCoreSize, kCoreSize}
                    : Rect{kMapW / 2, kMapH / 2, 1, 1};
}

// Steps the zoom by `delta` (positive = closer), keeping the world point under (px, py) in place.
void zoom_by(SDL_Renderer* r, Screen& s, CameraPos& cam, int delta, int px, int py) {
    const int target = std::clamp(s.scale + delta, s.limits.farthest, s.limits.closest);
    if (target == s.scale) {
        return;
    }
    const int old_scale = s.scale;
    s.zoom_offset = target - s.limits.standard;
    if (!refresh_screen(r, s)) {
        return;
    }
    cam = clamp_camera(zoom_about(cam, old_scale, s.scale, px, py), s.view);
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

void draw_hud(SDL_Renderer* r, const World& world, const Screen& s, Cell hover, bool probe_mode,
              Overlay overlay) {
    char line[192];
    char ground[64] = "";
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
    std::snprintf(line, sizeof line, "seed %016llx  zoom %dx %dx%d  gen %.1fms  cell %d,%d %s%s",
                  static_cast<unsigned long long>(m.seed), s.scale, s.view.w, s.view.h,
                  world.gen_ms, hover.x, hover.y, ground,
                  probe_mode ? "  [PROBE: click to place]" : "");
    const bool second = overlay != Overlay::Off;
    const SDL_FRect strip{0.0f, 0.0f, static_cast<float>(s.view.w), second ? 20.0f : 10.0f};
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

// Developer tool: renders the whole map once into a 3840x2160 target and saves it.
int run_map_shot(SDL_Renderer* r, std::uint64_t seed, const std::vector<Cell>& probes,
                 Overlay overlay, const char* path) {
    World world;
    MapView view(r);
    view.set_overlay(overlay, world.map, world.layers, world.fog); // applied by the first bake
    if (!new_map(world, view, seed)) {
        return 1;
    }
    if (probes.empty()) {
        world.fog.explore_all();
        view.update(world.map, world.fog, Rect{0, 0, kMapW, kMapH});
    }
    for (const Cell c : probes) {
        if (!place_probe(world, view, c)) {
            SDL_Log("probe at %d,%d doesn't fit (not flat buildable ground, or overlapping)", c.x,
                    c.y);
        }
    }
    SDL_Texture* full =
        SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kMapPxW, kMapPxH);
    if (full == nullptr) {
        SDL_Log("map target failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderTarget(r, full);
    view.draw({0, 0}, {kMapPxW, kMapPxH}, world.fog);
    for (const Cell c : world.probes) {
        view.draw_probe({0, 0}, c, kProbeColour);
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
    if (!SDL_CreateWindowAndRenderer("Pixel TD", kStandardViewW * 2, kStandardViewH * 2, flags,
                                     &window, &renderer)) {
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
    if (opts.map_shot != nullptr) {
        const int code =
            run_map_shot(renderer, first_seed, opts.probes, opts.overlay, opts.map_shot);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return code;
    }

    int exit_code = 0;
    {
        World world;
        MapView view(renderer);
        Screen screen;
        view.set_overlay(opts.overlay, world.map, world.layers, world.fog); // before the bake
        if (!new_map(world, view, first_seed) || !refresh_screen(renderer, screen)) {
            exit_code = 1;
        }
        for (const Cell c : opts.probes) {
            if (!place_probe(world, view, c)) {
                SDL_Log("probe at %d,%d doesn't fit (not flat buildable ground, or overlapping)",
                        c.x, c.y);
            }
        }
        CameraPos cam = centred_on(core_rect(world.map), screen.view);
        bool probe_mode = false;
        Cell hover{};

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
                        const auto s = static_cast<float>(screen.scale);
                        const int dx = static_cast<int>(drag_x / s);
                        const int dy = static_cast<int>(drag_y / s);
                        drag_x -= static_cast<float>(dx) * s;
                        drag_y -= static_cast<float>(dy) * s;
                        cam = clamp_camera({cam.x - dx, cam.y - dy}, screen.view);
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (probe_mode && e.button.button == SDL_BUTTON_LEFT) {
                        place_probe(world, view, hover);
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
            const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0 &&
                                 SDL_GetMouseFocus() == window;

            const Uint64 now = SDL_GetTicksNS();
            const int ticks = step.advance(static_cast<std::int64_t>(now - last));
            last = now;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const int pan = std::max(1, kPanPerTick / screen.scale);
            for (int i = 0; i < ticks; ++i) {
                int dx = 0;
                int dy = 0;
                if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) {
                    dx -= pan;
                }
                if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) {
                    dx += pan;
                }
                if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
                    dy -= pan;
                }
                if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
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

            // The cell under the cursor; in probe mode, the probe's top-left so the cursor sits
            // on its centre.
            const int wx = cam.x + static_cast<int>(mx * density) / screen.scale;
            const int wy = cam.y + static_cast<int>(my * density) / screen.scale;
            hover = probe_mode ? Cell{(wx - kCellPx / 2) / kCellPx, (wy - kCellPx / 2) / kCellPx}
                               : Cell{wx / kCellPx, wy / kCellPx};

            SDL_SetRenderTarget(renderer, screen.target);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            view.draw(cam, screen.view, world.fog);
            for (const Cell c : world.probes) {
                view.draw_probe(cam, c, kProbeColour);
            }
            if (probe_mode) {
                view.draw_probe_preview(cam, world.map, world.params, hover,
                                        probe_fits(world, hover), world.probe_range_px());
            }
            draw_hud(renderer, world, screen, hover, probe_mode, view.overlay());

            ++frame;
            const bool last_frame = opts.frames > 0 && frame >= opts.frames;
            if (last_frame && opts.screenshot != nullptr &&
                !save_target(renderer, opts.screenshot)) {
                exit_code = 1;
            }

            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            const SDL_FRect dst{0.0f, 0.0f, static_cast<float>(screen.view.w * screen.scale),
                                static_cast<float>(screen.view.h * screen.scale)};
            SDL_RenderTexture(renderer, screen.target, nullptr, &dst);
            SDL_RenderPresent(renderer);

            if (last_frame) {
                running = false;
            }
        }
        if (screen.target != nullptr) {
            SDL_DestroyTexture(screen.target);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
