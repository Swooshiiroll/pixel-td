#include "game/map_view.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/los.hpp"

namespace pixeltd::game {
namespace {

using Rgbf = std::array<float, 3>;

// Placeholder palette (a dusty planet surface) until the style board picks the art direction.
// The flat ground (#30) is coloured by its material; rock formations are shaded by how tall they
// stand and cast a shadow to the south-east.
constexpr std::array<Rgbf, kMaterials> kMaterialColour = {{
    {196.0f, 152.0f, 108.0f}, // dust
    {160.0f, 150.0f, 138.0f}, // gravel
    {128.0f, 122.0f, 130.0f}, // ash
    {190.0f, 116.0f, 82.0f},  // rust
}};
constexpr Rgbf kFormationColour{132.0f, 124.0f, 142.0f};
constexpr Rgbf kNeutral{176.0f, 158.0f, 140.0f}; // materials are muted a little towards this

// The fog layer: unexplored cells are pulled this far towards this colour.
constexpr std::uint8_t kHazeR = 58;
constexpr std::uint8_t kHazeG = 62;
constexpr std::uint8_t kHazeB = 80;
constexpr std::uint8_t kHazeAlpha = 115; // 0.45 * 255

constexpr std::size_t cell_index(int x, int y) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(kMapW) +
           static_cast<std::size_t>(x);
}

constexpr std::size_t cell_bytes() noexcept {
    return static_cast<std::size_t>(kMapW) * static_cast<std::size_t>(kMapH) * 4u;
}

} // namespace

MapView::MapView(SDL_Renderer* renderer)
    : renderer_(renderer), cell_(static_cast<std::size_t>(kMapW) * static_cast<std::size_t>(kMapH)),
      terrain_px_(cell_bytes(), 0), fog_px_(cell_bytes(), 0) {}

MapView::~MapView() {
    if (terrain_ != nullptr) {
        SDL_DestroyTexture(terrain_);
    }
    if (fog_ != nullptr) {
        SDL_DestroyTexture(fog_);
    }
}

bool MapView::create_textures() {
    // Both are one texel per cell, stretched by the GPU with linear filtering: each pixel blends
    // the four nearest cell centres, so the terrain (and the haze) is seamless at any size.
    terrain_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kMapW,
                                 kMapH);
    fog_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kMapW,
                             kMapH);
    if (terrain_ == nullptr || fog_ == nullptr) {
        SDL_Log("map textures failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(terrain_, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureScaleMode(fog_, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(fog_, SDL_BLENDMODE_BLEND);
    return true;
}

bool MapView::set_map(const Map& map, const GenLayers& layers, const Fog& fog) {
    const Uint64 start = SDL_GetTicksNS();
    if (terrain_ == nullptr && !create_textures()) {
        return false;
    }
    core_ = map.core;
    spawns_ = map.spawns;
    fill_cells(map, layers);
    const bool ok = upload_terrain() && update_fog(fog, Rect{0, 0, kMapW, kMapH});
    bake_ms_ = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    return ok;
}

bool MapView::set_overlay(Overlay overlay, const Map& map, const GenLayers& layers,
                          const Fog& fog) {
    (void)fog; // the fog is its own layer; an overlay doesn't change it
    overlay_ = overlay;
    if (terrain_ == nullptr) {
        return true; // applied by the first set_map
    }
    const Uint64 start = SDL_GetTicksNS();
    fill_cells(map, layers);
    const bool ok = upload_terrain();
    bake_ms_ = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    return ok;
}

bool MapView::upload_terrain() {
    for (std::size_t i = 0; i < cell_.size(); ++i) {
        std::uint8_t* out = terrain_px_.data() + i * 4u;
        for (std::size_t ch = 0; ch < 3; ++ch) {
            out[ch] = static_cast<std::uint8_t>(std::clamp(cell_[i][ch], 0.0f, 255.0f));
        }
        out[3] = 255;
    }
    if (!SDL_UpdateTexture(terrain_, nullptr, terrain_px_.data(), kMapW * 4)) {
        SDL_Log("terrain upload failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void MapView::fill_cells(const Map& map, const GenLayers& layers) {
    if (overlay_ != Overlay::Off) {
        // A noise layer: greyscale, stretched over the layer's own lowest to highest value.
        const Grid<int>* field = overlay_ == Overlay::Rough ? &layers.rough : &layers.material;
        const auto [lo_it, hi_it] =
            std::minmax_element(field->cells().begin(), field->cells().end());
        const int lo = *lo_it;
        const int span = std::max(1, *hi_it - lo);
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                const float g = layers.valid ? 30.0f + static_cast<float>(field->at(x, y) - lo) *
                                                           205.0f / static_cast<float>(span)
                                             : 128.0f; // the fallback map has no noise
                cell_[cell_index(x, y)] = {g, g, g};
            }
        }
        return;
    }

    // The terrain. The flat ground's colour is its material hue, softened over its neighbours and
    // muted a little. Rock formations are shaded by how tall they stand (their 1..255 value) and
    // lit from the north-west, and cast a shadow to the south-east.
    const auto rock = [&](int x, int y) {
        return map.formation.in_bounds(x, y) ? static_cast<float>(map.formation.at(x, y)) : 0.0f;
    };
    const auto rock_at = [&](int x, int y) { return rock(x, y) > 0.0f; };
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            Rgbf base{0.0f, 0.0f, 0.0f};
            float k = 0.86f;
            if (rock_at(x, y)) {
                base = kFormationColour;
                const float gx = rock(x + 1, y) - rock(x - 1, y);
                const float gy = rock(x, y + 1) - rock(x, y - 1);
                k = (1.0f + 0.4f * rock(x, y) / 255.0f) *
                    std::clamp(1.0f - (gx + gy) * 0.004f, 0.55f, 1.4f);
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
                    base[ch] = base[ch] * 0.8f + kNeutral[ch] * 0.2f;
                }
            }
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
}

bool MapView::update_fog(const Fog& fog, const Rect& cells) {
    if (fog_ == nullptr) {
        return false;
    }
    const int x0 = std::max(0, cells.x);
    const int y0 = std::max(0, cells.y);
    const int x1 = std::min(kMapW, cells.x + cells.w);
    const int y1 = std::min(kMapH, cells.y + cells.h);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            std::uint8_t* t = fog_px_.data() + cell_index(x, y) * 4u;
            t[0] = kHazeR;
            t[1] = kHazeG;
            t[2] = kHazeB;
            // Haze over unexplored cells, once there's a core.
            t[3] = core_ && !fog.explored({x, y}) ? kHazeAlpha : 0;
        }
    }
    // The whole layer is one texel per cell (about 2 MB): uploading it all is simpler than
    // tracking regions, and it only happens when fog lifts.
    if (!SDL_UpdateTexture(fog_, nullptr, fog_px_.data(), kMapW * 4)) {
        SDL_Log("fog upload failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void MapView::draw(const DrawXf& xf, ViewSize view, const Fog& fog) const {
    // The part of the map under the camera, in world pixels. When the view is bigger than the map
    // (centred, so the camera is negative), clip it to the map.
    float sx = static_cast<float>(xf.cam.x);
    float sy = static_cast<float>(xf.cam.y);
    float sw = static_cast<float>(view.w);
    float sh = static_cast<float>(view.h);
    if (sx < 0.0f) {
        sw += sx;
        sx = 0.0f;
    }
    if (sy < 0.0f) {
        sh += sy;
        sy = 0.0f;
    }
    sw = std::min(sw, static_cast<float>(kMapPxW) - sx);
    sh = std::min(sh, static_cast<float>(kMapPxH) - sy);
    const SDL_FRect dst{xf.x(sx), xf.y(sy), sw * xf.k, sh * xf.k};

    // Terrain: the cell texture, one texel per cell, so the source rectangle is in cells. At
    // whole-number steps this lands 1:1 in world pixels in the view target, scaled up crisp later.
    const auto c = static_cast<float>(kCellPx);
    const SDL_FRect src{sx / c, sy / c, sw / c, sh / c};
    SDL_RenderTexture(renderer_, terrain_, &src, &dst);

    // Fog: one texel per cell, stretched with linear filtering.
    if (fog_shown_ && core_) {
        SDL_RenderTexture(renderer_, fog_, &src, &dst);
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (core_) {
        // The core: a placeholder reactor (frame, body, glowing heart).
        const SDL_FRect frame = xf.rect(rect_of(*core_, kCoreSub));
        SDL_SetRenderDrawColor(renderer_, 28, 34, 52, 255);
        SDL_RenderFillRect(renderer_, &frame);
        const float b = 4.0f * xf.k;
        const SDL_FRect body{frame.x + b, frame.y + b, frame.w - 2.0f * b, frame.h - 2.0f * b};
        SDL_SetRenderDrawColor(renderer_, 70, 96, 128, 255);
        SDL_RenderFillRect(renderer_, &body);
        const float hrt = 24.0f * xf.k;
        const SDL_FRect heart{frame.x + hrt, frame.y + hrt, frame.w - 2.0f * hrt,
                              frame.h - 2.0f * hrt};
        SDL_SetRenderDrawColor(renderer_, 110, 230, 255, 255);
        SDL_RenderFillRect(renderer_, &heart);
        SDL_SetRenderDrawColor(renderer_, 140, 200, 230, 255);
        SDL_RenderRect(renderer_, &frame);
    }

    // Spawns: red markers labelled with the wave they open at, once explored (or with the fog
    // hidden).
    for (const Spawn& s : spawns_) {
        if (fog_shown_ && !fog.spawn_found(s)) {
            continue;
        }
        const SDL_FRect r =
            xf.rect(RectPx{s.cell.x * kCellPx, s.cell.y * kCellPx, 2 * kCellPx, 2 * kCellPx});
        SDL_SetRenderDrawColor(renderer_, 220, 64, 52, 255);
        SDL_RenderFillRect(renderer_, &r);
        SDL_SetRenderDrawColor(renderer_, 255, 190, 120, 255);
        SDL_RenderRect(renderer_, &r);
        char label[8];
        std::snprintf(label, sizeof label, "%d", s.opens_at_wave);
        SDL_SetRenderDrawColor(renderer_, 255, 240, 220, 255);
        SDL_RenderDebugText(renderer_, r.x + r.w + 2.0f, r.y + r.h / 2.0f - 4.0f, label);
    }
}

void MapView::draw_probe_preview(const DrawXf& xf, const Map& map, SubCell top_left, bool legal,
                                 int range_px) const {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const PointPx centre = centre_of(top_left, kLightSub);
    // Within its reach (a circle): cells it sees get a light tint; cells hidden behind rock
    // formations are shaded, so blocked sight lines show.
    const int reach = range_px;
    const long long reach2 = static_cast<long long>(reach) * reach;
    for (int cy = (centre.y - reach) / kCellPx; cy <= (centre.y + reach) / kCellPx; ++cy) {
        for (int cx = (centre.x - reach) / kCellPx; cx <= (centre.x + reach) / kCellPx; ++cx) {
            if (!map.formation.in_bounds(cx, cy)) {
                continue;
            }
            const PointPx c = centre_of(Cell{cx, cy});
            const long long dx = c.x - centre.x;
            const long long dy = c.y - centre.y;
            const long long d2 = dx * dx + dy * dy;
            if (d2 > reach2) {
                continue;
            }
            const bool covered = can_see(map, centre, c);
            if (covered) {
                SDL_SetRenderDrawColor(renderer_, 250, 220, 120, 40);
            } else {
                SDL_SetRenderDrawColor(renderer_, 10, 8, 20, 120);
            }
            const SDL_FRect cell = xf.rect(RectPx{cx * kCellPx, cy * kCellPx, kCellPx, kCellPx});
            SDL_RenderFillRect(renderer_, &cell);
        }
    }
    draw_probe(xf, top_left, legal ? SDL_Color{250, 220, 120, 200} : SDL_Color{240, 80, 60, 200});
}

void MapView::draw_probe(const DrawXf& xf, SubCell top_left, SDL_Color colour) const {
    const SDL_FRect r = xf.rect(rect_of(top_left, kLightSub));
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, colour.r, colour.g, colour.b, colour.a);
    SDL_RenderFillRect(renderer_, &r);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 220);
    SDL_RenderRect(renderer_, &r);
}

} // namespace pixeltd::game
