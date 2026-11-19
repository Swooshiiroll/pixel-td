#include "game/map_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace pixeltd::game {
namespace {

struct Rgbf {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// Placeholder palette (a dusty planet surface) until the style board picks the art direction.
// Hue comes from the material; brightness comes from height and slope.
constexpr std::array<Rgbf, kMaterials> kMaterialColour = {{
    {196.0f, 152.0f, 108.0f}, // dust
    {160.0f, 150.0f, 138.0f}, // gravel
    {128.0f, 122.0f, 130.0f}, // ash
    {190.0f, 116.0f, 82.0f},  // rust
}};
constexpr Rgbf kFormationColour{132.0f, 124.0f, 142.0f};
constexpr Rgbf kNeutral{176.0f, 158.0f, 140.0f}; // materials are muted towards this

} // namespace

MapView::MapView(SDL_Renderer* renderer)
    : renderer_(renderer),
      pixels_(static_cast<std::size_t>(kMapPxW) * static_cast<std::size_t>(kMapPxH) * 4u, 0) {}

MapView::~MapView() {
    if (terrain_ != nullptr) {
        SDL_DestroyTexture(terrain_);
    }
}

bool MapView::set_map(const Map& map) {
    const Uint64 start = SDL_GetTicksNS();
    if (terrain_ == nullptr) {
        terrain_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                     kMapPxW, kMapPxH);
        if (terrain_ == nullptr) {
            SDL_Log("terrain texture failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureScaleMode(terrain_, SDL_SCALEMODE_NEAREST);
    }

    // 1. Each cell's final colour: its material hue (softened over its neighbours and muted, so
    //    height reads first), times brightness from height, hill shading from the slope (lit from
    //    the north-west), and the shadow rock formations cast to the south-east.
    const std::size_t n = static_cast<std::size_t>(kMapW) * static_cast<std::size_t>(kMapH);
    const auto idx = [](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(kMapW) +
               static_cast<std::size_t>(x);
    };
    const auto h_at = [&](int x, int y) {
        return static_cast<float>(
            map.height.at(std::clamp(x, 0, kMapW - 1), std::clamp(y, 0, kMapH - 1)));
    };
    const auto rock_at = [&](int x, int y) {
        return map.formation.in_bounds(x, y) && map.formation.at(x, y) != 0;
    };
    std::vector<Rgbf> cell(n);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            Rgbf base;
            if (rock_at(x, y)) {
                base = kFormationColour;
            } else {
                // Average the material hue over a 5x5 neighbourhood, then mute it.
                int count = 0;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int sx = std::clamp(x + dx, 0, kMapW - 1);
                        const int sy = std::clamp(y + dy, 0, kMapH - 1);
                        const Rgbf& m =
                            kMaterialColour[static_cast<std::size_t>(map.material.at(sx, sy))];
                        base.r += m.r;
                        base.g += m.g;
                        base.b += m.b;
                        ++count;
                    }
                }
                const float c = static_cast<float>(count);
                base = {base.r / c * 0.5f + kNeutral.r * 0.5f,
                        base.g / c * 0.5f + kNeutral.g * 0.5f,
                        base.b / c * 0.5f + kNeutral.b * 0.5f};
            }
            const float h = h_at(x, y);
            const float gx = h_at(x + 1, y) - h_at(x - 1, y);
            const float gy = h_at(x, y + 1) - h_at(x, y - 1);
            float k =
                (0.28f + 0.87f * h / 255.0f) * std::clamp(1.0f - (gx + gy) * 0.012f, 0.4f, 1.5f);
            if (!rock_at(x, y)) {
                for (int d = 1; d <= 4; ++d) {
                    if (rock_at(x - d, y - d)) {
                        k *= 0.62f; // in the shadow of a formation
                        break;
                    }
                }
            }
            cell[idx(x, y)] = {base.r * k, base.g * k, base.b * k};
        }
    }

    // 2. Each pixel blends the four nearest cell centres, so there are no visible tiles.
    for (int py = 0; py < kMapPxH; ++py) {
        const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(kCellPx) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, kMapH - 1);
        const int y1 = std::min(y0 + 1, kMapH - 1);
        const float fy = std::clamp(v - static_cast<float>(y0), 0.0f, 1.0f);
        std::uint8_t* out =
            pixels_.data() + static_cast<std::size_t>(py) * static_cast<std::size_t>(kMapPxW) * 4u;
        for (int px = 0; px < kMapPxW; ++px, out += 4) {
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(kCellPx) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, kMapW - 1);
            const int x1 = std::min(x0 + 1, kMapW - 1);
            const float fx = std::clamp(u - static_cast<float>(x0), 0.0f, 1.0f);
            const Rgbf& a = cell[idx(x0, y0)];
            const Rgbf& b = cell[idx(x1, y0)];
            const Rgbf& c = cell[idx(x0, y1)];
            const Rgbf& d = cell[idx(x1, y1)];
            const float wa = (1.0f - fx) * (1.0f - fy);
            const float wb = fx * (1.0f - fy);
            const float wc = (1.0f - fx) * fy;
            const float wd = fx * fy;
            const auto ch = [](float val) {
                return static_cast<std::uint8_t>(std::clamp(val, 0.0f, 255.0f));
            };
            out[0] = ch(a.r * wa + b.r * wb + c.r * wc + d.r * wd);
            out[1] = ch(a.g * wa + b.g * wb + c.g * wc + d.g * wd);
            out[2] = ch(a.b * wa + b.b * wb + c.b * wc + d.b * wd);
            out[3] = 255;
        }
    }
    core_ = map.core;
    spawns_ = map.spawns;
    const bool ok = SDL_UpdateTexture(terrain_, nullptr, pixels_.data(), kMapPxW * 4);
    if (!ok) {
        SDL_Log("terrain upload failed: %s", SDL_GetError());
    }
    bake_ms_ = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    return ok;
}

void MapView::draw(CameraPos cam, ViewSize view) const {
    // The part of the terrain texture under the camera. When the view is bigger than the map
    // (centred, so the camera is negative), clip the source and shift the destination to match.
    SDL_FRect src{static_cast<float>(cam.x), static_cast<float>(cam.y), static_cast<float>(view.w),
                  static_cast<float>(view.h)};
    SDL_FRect dst{0.0f, 0.0f, src.w, src.h};
    if (src.x < 0.0f) {
        dst.x = -src.x;
        src.w += src.x;
        src.x = 0.0f;
    }
    if (src.y < 0.0f) {
        dst.y = -src.y;
        src.h += src.y;
        src.y = 0.0f;
    }
    src.w = std::min(src.w, static_cast<float>(kMapPxW) - src.x);
    src.h = std::min(src.h, static_cast<float>(kMapPxH) - src.y);
    dst.w = src.w;
    dst.h = src.h;
    SDL_RenderTexture(renderer_, terrain_, &src, &dst);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const auto world_rect = [&](Cell c, int size) {
        return SDL_FRect{static_cast<float>(c.x * kCellPx - cam.x),
                         static_cast<float>(c.y * kCellPx - cam.y),
                         static_cast<float>(size * kCellPx), static_cast<float>(size * kCellPx)};
    };

    if (core_) {
        // The core: a placeholder reactor (frame, body, glowing heart).
        const SDL_FRect frame = world_rect(*core_, kCoreSize);
        SDL_SetRenderDrawColor(renderer_, 28, 34, 52, 255);
        SDL_RenderFillRect(renderer_, &frame);
        const SDL_FRect body{frame.x + 4.0f, frame.y + 4.0f, frame.w - 8.0f, frame.h - 8.0f};
        SDL_SetRenderDrawColor(renderer_, 70, 96, 128, 255);
        SDL_RenderFillRect(renderer_, &body);
        const SDL_FRect heart{frame.x + 24.0f, frame.y + 24.0f, frame.w - 48.0f, frame.h - 48.0f};
        SDL_SetRenderDrawColor(renderer_, 110, 230, 255, 255);
        SDL_RenderFillRect(renderer_, &heart);
        SDL_SetRenderDrawColor(renderer_, 140, 200, 230, 255);
        SDL_RenderRect(renderer_, &frame);
    }

    // Spawns: red markers, labelled with the wave they open at.
    for (const Spawn& s : spawns_) {
        const SDL_FRect r = world_rect(s.cell, 2);
        SDL_SetRenderDrawColor(renderer_, 220, 64, 52, 255);
        SDL_RenderFillRect(renderer_, &r);
        SDL_SetRenderDrawColor(renderer_, 255, 190, 120, 255);
        SDL_RenderRect(renderer_, &r);
        char label[8];
        std::snprintf(label, sizeof label, "%d", s.opens_at_wave);
        SDL_SetRenderDrawColor(renderer_, 255, 240, 220, 255);
        SDL_RenderDebugText(renderer_, r.x + r.w + 2.0f, r.y + 8.0f, label);
    }
}

} // namespace pixeltd::game
