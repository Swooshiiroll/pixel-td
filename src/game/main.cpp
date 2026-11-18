// Pixel TD: SDL3 front end. Everything is drawn into a 640x360 render target, then shown at the
// largest whole-number scale that fits the window (letterboxed), so pixels stay square and crisp.
//
//   PixelTD [--frames N] [--screenshot file.bmp]
//     --frames N        quit after N frames (smoke tests)
//     --screenshot F    with --frames: save the last 640x360 frame to F (BMP)

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/fixed_step.hpp"

namespace {

constexpr int kViewW = 640;
constexpr int kViewH = 360;
constexpr int kTile = 32;
constexpr int kCols = kViewW / kTile; // 20
constexpr int kRows = kViewH / kTile; // 11; the 8 px left at the bottom is the status strip
constexpr std::int64_t kTickNs = 1'000'000'000 / 60;
constexpr int kMaxTicksPerFrame = 8;

struct Options {
    long frames = 0;
    const char* screenshot = nullptr;
};

bool parse_options(int argc, char** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            out.frames = std::strtol(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            out.screenshot = argv[++i];
        } else {
            std::fprintf(stderr, "usage: PixelTD [--frames N] [--screenshot file.bmp]\n");
            return false;
        }
    }
    return true;
}

// Placeholder scene state until the game exists: one marker running along the middle row.
struct Scene {
    float x = 0.0f;
    float speed = 2.0f; // px per tick
};

void update(Scene& s) {
    s.x += s.speed;
    const float max_x = static_cast<float>(kViewW - kTile);
    if (s.x < 0.0f || s.x > max_x) {
        s.speed = -s.speed;
        s.x = std::clamp(s.x, 0.0f, max_x);
    }
}

// Where the 640x360 view lands in a window of out_w x out_h pixels.
SDL_FRect view_rect(int out_w, int out_h) {
    const int scale = std::min(out_w / kViewW, out_h / kViewH);
    if (scale < 1) { // window smaller than the view: shrink to fit (not pixel-perfect)
        const float f =
            std::min(static_cast<float>(out_w) / kViewW, static_cast<float>(out_h) / kViewH);
        const float w = kViewW * f;
        const float h = kViewH * f;
        return {(static_cast<float>(out_w) - w) / 2.0f, (static_cast<float>(out_h) - h) / 2.0f, w,
                h};
    }
    const int w = kViewW * scale;
    const int h = kViewH * scale;
    return {static_cast<float>((out_w - w) / 2), static_cast<float>((out_h - h) / 2),
            static_cast<float>(w), static_cast<float>(h)};
}

void draw_scene(SDL_Renderer* r, const Scene& prev, const Scene& cur, float alpha,
                const char* status) {
    SDL_SetRenderDrawColor(r, 12, 10, 24, 255);
    SDL_RenderClear(r);

    // Build-tile checkerboard, with a lane along the middle row.
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const bool lane = row == kRows / 2;
            const bool dark = ((row + col) & 1) != 0;
            if (lane) {
                SDL_SetRenderDrawColor(r, 70, 58, 44, 255);
            } else if (dark) {
                SDL_SetRenderDrawColor(r, 28, 34, 52, 255);
            } else {
                SDL_SetRenderDrawColor(r, 34, 42, 62, 255);
            }
            const SDL_FRect tile = {static_cast<float>(col * kTile),
                                    static_cast<float>(row * kTile), static_cast<float>(kTile),
                                    static_cast<float>(kTile)};
            SDL_RenderFillRect(r, &tile);
        }
    }

    // The marker, interpolated between the last two ticks and snapped to the pixel grid.
    const float x = prev.x + (cur.x - prev.x) * alpha;
    const SDL_FRect marker = {SDL_floorf(x) + 8.0f, static_cast<float>((kRows / 2) * kTile + 8),
                              16.0f, 16.0f};
    SDL_SetRenderDrawColor(r, 255, 140, 48, 255);
    SDL_RenderFillRect(r, &marker);

    SDL_SetRenderDrawColor(r, 160, 230, 200, 255);
    SDL_RenderDebugText(r, 2.0f, static_cast<float>(kRows * kTile), status);
}

bool save_view(SDL_Renderer* r, const char* path) {
    SDL_Surface* shot = SDL_RenderReadPixels(r, nullptr);
    if (shot == nullptr) {
        SDL_Log("screenshot failed: %s", SDL_GetError());
        return false;
    }
    const bool ok = SDL_SaveBMP(shot, path);
    if (!ok) {
        SDL_Log("saving %s failed: %s", path, SDL_GetError());
    }
    SDL_DestroySurface(shot);
    return ok;
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

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Pixel TD", kViewW * 2, kViewH * 2,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window,
                                     &renderer)) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    SDL_Texture* view = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, kViewW, kViewH);
    if (view == nullptr) {
        SDL_Log("render target failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(view, SDL_SCALEMODE_NEAREST);

    const char* backend = SDL_GetRendererName(renderer);
    SDL_Log("Pixel TD %s, SDL %d.%d.%d, renderer: %s", PIXELTD_VERSION,
            SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
            SDL_VERSIONNUM_MICRO(SDL_GetVersion()), backend);

    pixeltd::FixedStep step(kTickNs, kMaxTicksPerFrame);
    Scene prev;
    Scene cur;
    char status[96];
    Uint64 last = SDL_GetTicksNS();
    long frame = 0;
    int exit_code = 0;
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT ||
                (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }

        const Uint64 now = SDL_GetTicksNS();
        const int ticks = step.advance(static_cast<std::int64_t>(now - last));
        last = now;
        for (int i = 0; i < ticks; ++i) {
            prev = cur;
            update(cur);
        }

        std::snprintf(status, sizeof status, "Pixel TD %s  |  %s  |  tick %llu", PIXELTD_VERSION,
                      backend, static_cast<unsigned long long>(step.total_ticks()));

        SDL_SetRenderTarget(renderer, view);
        draw_scene(renderer, prev, cur, step.alpha(), status);

        ++frame;
        const bool last_frame = opts.frames > 0 && frame >= opts.frames;
        if (last_frame && opts.screenshot != nullptr && !save_view(renderer, opts.screenshot)) {
            exit_code = 1;
        }

        SDL_SetRenderTarget(renderer, nullptr);
        int out_w = 0;
        int out_h = 0;
        SDL_GetRenderOutputSize(renderer, &out_w, &out_h);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const SDL_FRect dst = view_rect(out_w, out_h);
        SDL_RenderTexture(renderer, view, nullptr, &dst);
        SDL_RenderPresent(renderer);

        if (last_frame) {
            running = false;
        }
    }

    SDL_DestroyTexture(view);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
