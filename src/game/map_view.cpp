#include "game/map_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

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
constexpr Rgbf kMesaTint{230.0f, 140.0f, 60.0f}; // the mesa overlay tints terraced regions

// The fog layer: unexplored cells are pulled this far towards this colour.
constexpr std::uint8_t kHazeR = 58;
constexpr std::uint8_t kHazeG = 62;
constexpr std::uint8_t kHazeB = 80;
constexpr std::uint8_t kHazeAlpha = 115; // 0.45 * 255

constexpr std::size_t cell_index(int x, int y) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(kMapW) +
           static_cast<std::size_t>(x);
}

constexpr std::size_t pixel_bytes(int w, int h) noexcept {
    return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
}

// Averages 2x2 pixel blocks of an RGBA image (w x h, both even) into one half the size.
void halve(const std::vector<std::uint8_t>& in, int w, int h, std::vector<std::uint8_t>& out) {
    const int hw = w / 2;
    const int hh = h / 2;
    const auto at = [&](int x, int y) {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                static_cast<std::size_t>(x)) *
               4u;
    };
    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < hw; ++x) {
            const std::size_t a = at(2 * x, 2 * y);
            const std::size_t b = at(2 * x + 1, 2 * y);
            const std::size_t c = at(2 * x, 2 * y + 1);
            const std::size_t d = at(2 * x + 1, 2 * y + 1);
            const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(hw) +
                                   static_cast<std::size_t>(x)) *
                                  4u;
            for (std::size_t ch = 0; ch < 4; ++ch) {
                out[o + ch] = static_cast<std::uint8_t>(
                    (in[a + ch] + in[b + ch] + in[c + ch] + in[d + ch] + 2) / 4);
            }
        }
    }
}

} // namespace

MapView::MapView(SDL_Renderer* renderer)
    : renderer_(renderer), pixels_(pixel_bytes(kMapPxW, kMapPxH), 0),
      half_(pixel_bytes(kMapPxW / 2, kMapPxH / 2), 0),
      quarter_(pixel_bytes(kMapPxW / 4, kMapPxH / 4), 0),
      cell_(static_cast<std::size_t>(kMapW) * static_cast<std::size_t>(kMapH)),
      fog_px_(pixel_bytes(kMapW, kMapH), 0) {}

MapView::~MapView() {
    for (SDL_Texture* t : terrain_) {
        if (t != nullptr) {
            SDL_DestroyTexture(t);
        }
    }
    if (fog_ != nullptr) {
        SDL_DestroyTexture(fog_);
    }
}

bool MapView::create_textures() {
    const std::array<std::array<int, 2>, kTerrainLevels> sizes = {
        {{kMapPxW, kMapPxH}, {kMapPxW / 2, kMapPxH / 2}, {kMapPxW / 4, kMapPxH / 4}}};
    for (std::size_t i = 0; i < terrain_.size(); ++i) {
        terrain_[i] = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                        sizes[i][0], sizes[i][1]);
        if (terrain_[i] == nullptr) {
            SDL_Log("terrain texture %zu failed: %s", i, SDL_GetError());
            return false;
        }
    }
    fog_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kMapW,
                             kMapH);
    if (fog_ == nullptr) {
        SDL_Log("fog texture failed: %s", SDL_GetError());
        return false;
    }
    // Linear: the haze blends smoothly between cell centres, like the terrain.
    SDL_SetTextureScaleMode(fog_, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(fog_, SDL_BLENDMODE_BLEND);
    return true;
}

bool MapView::set_map(const Map& map, const GenLayers& layers, const Fog& fog) {
    const Uint64 start = SDL_GetTicksNS();
    if (terrain_[0] == nullptr && !create_textures()) {
        return false;
    }
    core_ = map.core;
    spawns_ = map.spawns;
    fill_cells(map, layers);
    const bool ok = render(map) && update_fog(fog, Rect{0, 0, kMapW, kMapH});
    bake_ms_ = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    return ok;
}

bool MapView::set_overlay(Overlay overlay, const Map& map, const GenLayers& layers,
                          const Fog& fog) {
    (void)fog; // the fog is its own layer; an overlay doesn't change it
    if (terrain_[0] == nullptr) {
        overlay_ = overlay; // applied by the first set_map
        return true;
    }
    const Uint64 start = SDL_GetTicksNS();
    overlay_ = overlay;
    fill_cells(map, layers);
    const bool ok = render(map);
    bake_ms_ = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
    return ok;
}

void MapView::fill_cells(const Map& map, const GenLayers& layers) {
    if (overlay_ == Overlay::Elevation) {
        return; // drawn per pixel from the heights (see render)
    }
    if (overlay_ != Overlay::Off) {
        // A noise layer: greyscale, stretched over the layer's own lowest to highest value.
        const Grid<int>* field = nullptr;
        switch (overlay_) {
        case Overlay::Octave1:
            field = &layers.octave[0];
            break;
        case Overlay::Octave2:
            field = &layers.octave[1];
            break;
        case Overlay::Octave3:
            field = &layers.octave[2];
            break;
        case Overlay::Height:
            field = &layers.height_sum;
            break;
        case Overlay::Mesa:
            field = &layers.mesa_noise;
            break;
        case Overlay::Rough:
            field = &layers.rough;
            break;
        default:
            field = &layers.material;
            break;
        }
        const auto [lo_it, hi_it] =
            std::minmax_element(field->cells().begin(), field->cells().end());
        const int lo = *lo_it;
        const int span = std::max(1, *hi_it - lo);
        for (int y = 0; y < kMapH; ++y) {
            for (int x = 0; x < kMapW; ++x) {
                const float g = layers.valid ? 30.0f + static_cast<float>(field->at(x, y) - lo) *
                                                           205.0f / static_cast<float>(span)
                                             : 128.0f; // the fallback map has no noise
                Rgbf c{g, g, g};
                if (overlay_ == Overlay::Mesa && layers.valid && layers.mesa.at(x, y) != 0) {
                    for (std::size_t ch = 0; ch < 3; ++ch) {
                        c[ch] = c[ch] * 0.55f + kMesaTint[ch] * 0.45f; // a terraced region
                    }
                }
                cell_[cell_index(x, y)] = c;
            }
        }
        return;
    }

    // The terrain. Each cell's colour: its material hue (softened over its neighbours and muted,
    // so height reads first), times brightness from height, hill shading from the slope (lit from
    // the north-west), and the shadow rock formations cast to the south-east.
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
    // The whole layer is 320 x 180 texels (230 KB): uploading it all is cheaper than tracking
    // regions.
    if (!SDL_UpdateTexture(fog_, nullptr, fog_px_.data(), kMapW * 4)) {
        SDL_Log("fog upload failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool MapView::render(const Map& map) {
    // Elevation: each pixel's height (in 1/16 units) blended between the four nearest cell
    // centres, so the colour bands and contour lines are smooth curves rather than cell steps.
    constexpr int kD = 2 * kCellPx; // a cell is kD sub-steps of half a pixel
    const auto height16 = [&](int px, int py) {
        const auto split = [](int p, int cells_n, int& c0, int& f) {
            const int u = 2 * p + 1 - kCellPx;
            c0 = u < 0 ? 0 : u / kD;
            f = u < 0 ? 0 : u % kD;
            if (c0 >= cells_n - 1) {
                c0 = cells_n - 1;
                f = 0;
            }
        };
        int x0 = 0;
        int fx = 0;
        int y0 = 0;
        int fy = 0;
        split(std::clamp(px, 0, kMapPxW - 1), kMapW, x0, fx);
        split(std::clamp(py, 0, kMapPxH - 1), kMapH, y0, fy);
        const int x1 = std::min(x0 + 1, kMapW - 1);
        const int y1 = std::min(y0 + 1, kMapH - 1);
        const int top = map.height.at(x0, y0) * (kD - fx) + map.height.at(x1, y0) * fx;
        const int bottom = map.height.at(x0, y1) * (kD - fx) + map.height.at(x1, y1) * fx;
        return (top * (kD - fy) + bottom * fy) * 16 / (kD * kD);
    };

    const bool elevation = overlay_ == Overlay::Elevation;
    std::vector<int> row;
    std::vector<int> below;
    if (elevation) {
        row.resize(static_cast<std::size_t>(kMapPxW + 1));
        below.resize(row.size());
        for (int px = 0; px <= kMapPxW; ++px) {
            row[static_cast<std::size_t>(px)] = height16(px, 0);
        }
    }
    for (int py = 0; py < kMapPxH; ++py) {
        const float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(kCellPx) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, kMapH - 1);
        const int y1 = std::min(y0 + 1, kMapH - 1);
        const float fy = std::clamp(v - static_cast<float>(y0), 0.0f, 1.0f);
        if (elevation) {
            for (int px = 0; px <= kMapPxW; ++px) {
                below[static_cast<std::size_t>(px)] = height16(px, py + 1);
            }
        }
        std::uint8_t* out = pixels_.data() + static_cast<std::size_t>(py) * kMapPxW * 4u;
        for (int px = 0; px < kMapPxW; ++px, out += 4) {
            if (elevation) {
                const auto i = static_cast<std::size_t>(px);
                const int h = row[i];
                const Rgb8 c = elevation_colour(h / 16);
                const int level =
                    std::max(contour_level(h, row[i + 1]), contour_level(h, below[i]));
                const float k = level == 2 ? 0.40f : level == 1 ? 0.70f : 1.0f;
                out[0] = static_cast<std::uint8_t>(static_cast<float>(c.r) * k);
                out[1] = static_cast<std::uint8_t>(static_cast<float>(c.g) * k);
                out[2] = static_cast<std::uint8_t>(static_cast<float>(c.b) * k);
                out[3] = 255;
                continue;
            }
            // Each pixel blends the four nearest cell centres, so there are no visible tiles.
            const float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(kCellPx) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, kMapW - 1);
            const int x1 = std::min(x0 + 1, kMapW - 1);
            const float fx = std::clamp(u - static_cast<float>(x0), 0.0f, 1.0f);
            const float wa = (1.0f - fx) * (1.0f - fy);
            const float wb = fx * (1.0f - fy);
            const float wc = (1.0f - fx) * fy;
            const float wd = fx * fy;
            const Rgbf& a = cell_[cell_index(x0, y0)];
            const Rgbf& b = cell_[cell_index(x1, y0)];
            const Rgbf& c = cell_[cell_index(x0, y1)];
            const Rgbf& d = cell_[cell_index(x1, y1)];
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const float val = a[ch] * wa + b[ch] * wb + c[ch] * wc + d[ch] * wd;
                out[ch] = static_cast<std::uint8_t>(std::clamp(val, 0.0f, 255.0f));
            }
            out[3] = 255;
        }
        if (elevation) {
            std::swap(row, below);
        }
    }

    // The shrunk copies for zooming out: each one averages 2x2 pixel blocks of the one before.
    halve(pixels_, kMapPxW, kMapPxH, half_);
    halve(half_, kMapPxW / 2, kMapPxH / 2, quarter_);
    const std::array<const std::vector<std::uint8_t>*, kTerrainLevels> levels = {&pixels_, &half_,
                                                                                 &quarter_};
    for (std::size_t i = 0; i < levels.size(); ++i) {
        const int pitch = (kMapPxW >> i) * 4;
        if (!SDL_UpdateTexture(terrain_[i], nullptr, levels[i]->data(), pitch)) {
            SDL_Log("terrain upload %zu failed: %s", i, SDL_GetError());
            return false;
        }
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

    // Terrain: the deepest copy whose scale is still at least the zoom, so the GPU never shrinks
    // it by more than 2x (unless the window is tiny). Whole-number steps draw level 0 crisp.
    const std::size_t level = xf.k > 0.5f ? 0 : xf.k > 0.25f ? 1 : 2;
    const float f = static_cast<float>(1 << level);
    SDL_Texture* terrain = terrain_[level];
    SDL_SetTextureScaleMode(terrain, xf.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    const SDL_FRect src{sx / f, sy / f, sw / f, sh / f};
    SDL_RenderTexture(renderer_, terrain, &src, &dst);

    // Fog: one texel per cell, stretched with linear filtering.
    if (fog_shown_ && core_) {
        const auto c = static_cast<float>(kCellPx);
        const SDL_FRect fsrc{sx / c, sy / c, sw / c, sh / c};
        SDL_RenderTexture(renderer_, fog_, &fsrc, &dst);
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

void MapView::draw_probe_preview(const DrawXf& xf, const Map& map, const GenParams& p,
                                 SubCell top_left, bool legal, int base_px) const {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const PointPx centre = centre_of(top_left, kLightSub);
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
