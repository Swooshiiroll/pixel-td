#include "game/map_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "core/los.hpp"

namespace pixeltd::game {
namespace {

using Rgbf = std::array<float, 3>;

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
constexpr Rgbf kHaze{58.0f, 62.0f, 80.0f};       // unexplored ground is pulled towards this

constexpr std::size_t cell_index(int x, int y) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(kMapW) +
           static_cast<std::size_t>(x);
}

} // namespace

MapView::MapView(SDL_Renderer* renderer)
    : renderer_(renderer),
      pixels_(static_cast<std::size_t>(kMapPxW) * static_cast<std::size_t>(kMapPxH) * 4u, 0),
      cell_(static_cast<std::size_t>(kMapW) * static_cast<std::size_t>(kMapH)),
      shown_(cell_.size()) {}

MapView::~MapView() {
    if (terrain_ != nullptr) {
        SDL_DestroyTexture(terrain_);
    }
}

bool MapView::set_map(const Map& map, const Fog& fog) {
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

    // Each cell's colour: its material hue (softened over its neighbours and muted, so height
    // reads first), times brightness from height, hill shading from the slope (lit from the
    // north-west), and the shadow rock formations cast to the south-east.
    const auto h_at = [&](int x, int y) {
        return static_cast<float>(
            map.height.at(std::clamp(x, 0, kMapW - 1), std::clamp(y, 0, kMapH - 1)));
    };
    const auto rock_at = [&](int x, int y) {
        return map.formation.in_bounds(x, y) && map.formation.at(x, y) != 0;
    };
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            Rgbf base{0.0f, 0.0f, 0.0f};
            if (rock_at(x, y)) {
                base = kFormationColour;
            } else {
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        const Rgbf& m = kMaterialColour[static_cast<std::size_t>(map.material.at(
                            std::clamp(x + dx, 0, kMapW - 1), std::clamp(y + dy, 0, kMapH - 1)))];
                        for (std::size_t ch = 0; ch < 3; ++ch) {
                            base[ch] += m[ch] / 25.0f;
                        }
                    }
                }
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    base[ch] = base[ch] * 0.5f + kNeutral[ch] * 0.5f;
                }
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
            cell_[cell_index(x, y)] = {base[0] * k, base[1] * k, base[2] * k};
        }
    }
    core_ = map.core;
    spawns_ = map.spawns;
    const bool ok = render(map, fog, Rect{0, 0, kMapW, kMapH});
    bake_ms_ = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    return ok;
}

bool MapView::update(const Map& map, const Fog& fog, const Rect& cells) {
    if (terrain_ == nullptr || cells.w <= 0 || cells.h <= 0) {
        return terrain_ != nullptr;
    }
    // One cell more on each side: pixels blend the neighbouring cell centres.
    const int x0 = std::max(0, cells.x - 1);
    const int y0 = std::max(0, cells.y - 1);
    const int x1 = std::min(kMapW, cells.x + cells.w + 1);
    const int y1 = std::min(kMapH, cells.y + cells.h + 1);
    return render(map, fog, Rect{x0, y0, x1 - x0, y1 - y0});
}

bool MapView::render(const Map& map, const Fog& fog, const Rect& cells) {
    // Haze over unexplored cells, once the core is placed.
    for (int y = cells.y; y < cells.y + cells.h; ++y) {
        for (int x = cells.x; x < cells.x + cells.w; ++x) {
            const std::size_t i = cell_index(x, y);
            shown_[i] = cell_[i];
            if (map.core && !fog.explored({x, y})) {
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    shown_[i][ch] = cell_[i][ch] * 0.55f + kHaze[ch] * 0.45f;
                }
            }
        }
    }
    // Each pixel blends the four nearest cell centres, so there are no visible tiles.
    const int px0 = cells.x * kCellPx;
    const int py0 = cells.y * kCellPx;
    const int px1 = (cells.x + cells.w) * kCellPx;
    const int py1 = (cells.y + cells.h) * kCellPx;
    for (int py = py0; py < py1; ++py) {
        const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(kCellPx) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, kMapH - 1);
        const int y1 = std::min(y0 + 1, kMapH - 1);
        const float fy = std::clamp(v - static_cast<float>(y0), 0.0f, 1.0f);
        std::uint8_t* out =
            pixels_.data() +
            (static_cast<std::size_t>(py) * kMapPxW + static_cast<std::size_t>(px0)) * 4u;
        for (int px = px0; px < px1; ++px, out += 4) {
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(kCellPx) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, kMapW - 1);
            const int x1 = std::min(x0 + 1, kMapW - 1);
            const float fx = std::clamp(u - static_cast<float>(x0), 0.0f, 1.0f);
            const Rgbf& a = shown_[cell_index(x0, y0)];
            const Rgbf& b = shown_[cell_index(x1, y0)];
            const Rgbf& c = shown_[cell_index(x0, y1)];
            const Rgbf& d = shown_[cell_index(x1, y1)];
            const float wa = (1.0f - fx) * (1.0f - fy);
            const float wb = fx * (1.0f - fy);
            const float wc = (1.0f - fx) * fy;
            const float wd = fx * fy;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const float val = a[ch] * wa + b[ch] * wb + c[ch] * wc + d[ch] * wd;
                out[ch] = static_cast<std::uint8_t>(std::clamp(val, 0.0f, 255.0f));
            }
            out[3] = 255;
        }
    }
    const SDL_Rect area{px0, py0, px1 - px0, py1 - py0};
    const std::size_t first =
        (static_cast<std::size_t>(py0) * kMapPxW + static_cast<std::size_t>(px0)) * 4u;
    if (!SDL_UpdateTexture(terrain_, &area, pixels_.data() + first, kMapPxW * 4)) {
        SDL_Log("terrain upload failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void MapView::draw(CameraPos cam, ViewSize view, const Fog& fog) const {
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

    // Spawns: red markers labelled with the wave they open at, once explored.
    for (const Spawn& s : spawns_) {
        if (!fog.spawn_found(s)) {
            continue;
        }
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

void MapView::draw_probe_preview(CameraPos cam, const Map& map, const GenParams& p, Cell top_left,
                                 bool legal, int base_px) const {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const PointPx centre = centre_of(top_left, kLightSize);
    const int tower = ground_at(map, centre);
    // Within the farthest possible reach: cells it reaches and sees get a light tint; the rest are
    // shaded, so the reach's real (non-circular) shape and blocked sight lines show.
    const int reach = base_px * (100 + p.range_bonus_max_pct) / 100;
    const long long reach2 = static_cast<long long>(reach) * reach;
    for (int cy = (centre.y - reach) / kCellPx; cy <= (centre.y + reach) / kCellPx; ++cy) {
        for (int cx = (centre.x - reach) / kCellPx; cx <= (centre.x + reach) / kCellPx; ++cx) {
            if (!map.height.in_bounds(cx, cy)) {
                continue;
            }
            const PointPx c = centre_of(Cell{cx, cy});
            const long long dx = c.x - centre.x;
            const long long dy = c.y - centre.y;
            const long long d2 = dx * dx + dy * dy;
            if (d2 > reach2) {
                continue;
            }
            const long long r = effective_range(base_px, tower, map.height.at(cx, cy), p);
            const bool covered = d2 <= r * r && can_see(map, centre, c, p);
            if (covered) {
                SDL_SetRenderDrawColor(renderer_, 250, 220, 120, 40);
            } else {
                SDL_SetRenderDrawColor(renderer_, 10, 8, 20, 120);
            }
            const SDL_FRect cell{static_cast<float>(cx * kCellPx - cam.x),
                                 static_cast<float>(cy * kCellPx - cam.y),
                                 static_cast<float>(kCellPx), static_cast<float>(kCellPx)};
            SDL_RenderFillRect(renderer_, &cell);
        }
    }
    draw_probe(cam, top_left, legal ? SDL_Color{250, 220, 120, 200} : SDL_Color{240, 80, 60, 200});
}

void MapView::draw_probe(CameraPos cam, Cell top_left, SDL_Color colour) const {
    const SDL_FRect r{static_cast<float>(top_left.x * kCellPx - cam.x),
                      static_cast<float>(top_left.y * kCellPx - cam.y),
                      static_cast<float>(kLightSize * kCellPx),
                      static_cast<float>(kLightSize * kCellPx)};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, colour.r, colour.g, colour.b, colour.a);
    SDL_RenderFillRect(renderer_, &r);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 220);
    SDL_RenderRect(renderer_, &r);
}

} // namespace pixeltd::game
