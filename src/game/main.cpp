// Pixel TD: SDL3 front end. The world is drawn 1:1 into a view texture of (window size / scale),
// where the scale is a whole number, then shown at that scale: pixels stay square and crisp at
// every zoom step, and the view always fills the window (design/world.md, Camera).
//
//   PixelTD [--seed N] [--map-shot file.bmp] [--frames N] [--screenshot file.bmp]
//     --seed N          developer tool: generate the map from seed N (decimal, or 0x hex)
//     --map-shot F      developer tool: save the whole 1920x720 map to F (BMP), then exit
//     --frames N        quit after N frames (smoke tests)
//     --screenshot F    with --frames: save the last view frame to F (BMP)
//
// Controls: pan with WASD / arrows / the window edges / right-drag; zoom with the mouse wheel or
// + and -; F5 makes a new random map (developer tool); Esc quits.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/camera.hpp"
#include "core/fixed_step.hpp"
#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/hash.hpp"
#include "core/map.hpp"
#include "core/mapgen.hpp"
#include "game/map_view.hpp"

namespace {

using namespace pixeltd;
using pixeltd::game::MapView;

constexpr std::int64_t kTickNs = 1'000'000'000 / 60;
constexpr int kMaxTicksPerFrame = 8;
constexpr int kPanPerTick = 18;  // window pixels per tick, the same speed at every zoom
constexpr float kEdgePan = 8.0f; // window points from the edge that start edge panning

struct Options {
    long frames = 0;
    const char* screenshot = nullptr;
    const char* map_shot = nullptr;
    bool has_seed = false;
    std::uint64_t seed = 0;
};

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
        } else {
            std::fprintf(stderr, "usage: PixelTD [--seed N] [--map-shot file.bmp] [--frames N] "
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
    double gen_ms = 0.0;
};

bool new_map(World& world, MapView& view, std::uint64_t seed) {
    const Uint64 start = SDL_GetTicksNS();
    world.map = generate_stage1(seed, world.params);
    world.gen_ms = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    const GenStats& s = world.map.stats;
    SDL_Log("map seed 0x%016llx: stage 1 in %.2f ms (attempt %d, zone try %d%s)",
            static_cast<unsigned long long>(seed), world.gen_ms, s.stage1_attempts, s.zone_tries,
            s.stage1_fallback ? ", FALLBACK" : "");
    return view.set_map(world.map);
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

CameraPos centred_on(const Rect& zone, ViewSize view) {
    const int cx = (zone.x * 2 + zone.w) * kCellPx / 2;
    const int cy = (zone.y * 2 + zone.h) * kCellPx / 2;
    return clamp_camera({cx - view.w / 2, cy - view.h / 2}, view);
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

void draw_hud(SDL_Renderer* r, const World& world, const Screen& s, Cell hover) {
    char line[160];
    const char* terrain = "";
    if (world.map.terrain.in_bounds(hover)) {
        switch (world.map.terrain.at(hover)) {
        case Terrain::Ground:
            terrain = "ground";
            break;
        case Terrain::High:
            terrain = "high";
            break;
        case Terrain::Rock:
            terrain = "rock";
            break;
        case Terrain::Cliff:
            terrain = "cliff";
            break;
        }
    }
    std::snprintf(line, sizeof line, "seed %016llx  zoom %dx %dx%d  gen %.1fms  cell %d,%d %s",
                  static_cast<unsigned long long>(world.map.seed), s.scale, s.view.w, s.view.h,
                  world.gen_ms, hover.x, hover.y, terrain);
    const SDL_FRect strip{0.0f, 0.0f, static_cast<float>(s.view.w), 10.0f};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
    SDL_RenderFillRect(r, &strip);
    SDL_SetRenderDrawColor(r, 170, 236, 210, 255);
    SDL_RenderDebugText(r, 2.0f, 1.0f, line);
}

// Developer tool: renders the whole map once into a 1920x720 target and saves it.
int run_map_shot(SDL_Renderer* r, std::uint64_t seed, const char* path) {
    World world;
    MapView view(r);
    if (!new_map(world, view, seed)) {
        return 1;
    }
    SDL_Texture* full =
        SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kMapPxW, kMapPxH);
    if (full == nullptr) {
        SDL_Log("map target failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderTarget(r, full);
    view.draw({0, 0}, {kMapPxW, kMapPxH});
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
        const int code = run_map_shot(renderer, first_seed, opts.map_shot);
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
        if (!new_map(world, view, first_seed) || !refresh_screen(renderer, screen)) {
            exit_code = 1;
        }
        CameraPos cam = centred_on(world.map.zone, screen.view);

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
                        cam = centred_on(world.map.zone, screen.view);
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

            const Cell hover{(cam.x + static_cast<int>(mx * density) / screen.scale) / kCellPx,
                             (cam.y + static_cast<int>(my * density) / screen.scale) / kCellPx};

            SDL_SetRenderTarget(renderer, screen.target);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            view.draw(cam, screen.view);
            draw_hud(renderer, world, screen, hover);

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
