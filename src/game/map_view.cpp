#include "game/map_view.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace pixeltd::game {
namespace {

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

constexpr Rgb shade(Rgb c, int d) noexcept {
    return {std::clamp(c.r + d, 0, 255), std::clamp(c.g + d, 0, 255), std::clamp(c.b + d, 0, 255)};
}

// Placeholder palette (dusty planet surface) until the style board picks the art direction.
constexpr Rgb kGround{78, 58, 46};
constexpr Rgb kHigh{150, 114, 80};
constexpr Rgb kHighRim{184, 146, 104};
constexpr Rgb kHighFace{98, 72, 54};
constexpr Rgb kRock{126, 122, 132};
constexpr Rgb kRockEdge{70, 66, 78};
constexpr Rgb kCliff{50, 48, 64};
constexpr Rgb kCliffTop{88, 86, 106};

// Cheap integer hash for per-cell and per-pixel variation (looks only; not part of the sim).
constexpr std::uint32_t mix(std::uint32_t x, std::uint32_t y) noexcept {
    std::uint32_t h = x * 73856093u ^ y * 19349663u;
    h ^= h >> 13u;
    h *= 0x5bd1e995u;
    h ^= h >> 15u;
    return h;
}

Rgb pixel_colour(const Grid<Terrain>& t, int cx, int cy, int px, int py) {
    const Terrain here = t.at(cx, cy);
    const auto at = [&](int dx, int dy) {
        return t.in_bounds(cx + dx, cy + dy) ? t.at(cx + dx, cy + dy) : here;
    };
    const int cell_var =
        static_cast<int>(mix(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy)) % 7u) -
        3;
    const std::uint32_t grain = mix(static_cast<std::uint32_t>(cx * kCellPx + px),
                                    static_cast<std::uint32_t>(cy * kCellPx + py));
    const int last = kCellPx - 1;

    switch (here) {
    case Terrain::Ground: {
        Rgb c = shade(kGround, cell_var);
        if (px == 0 || py == 0) {
            c = shade(c, -5); // faint 12 px grid
        }
        if (grain % 23u == 0u) {
            c = shade(c, 7);
        }
        // Shadows cast down onto low ground by plateaus and cliffs.
        if (at(0, -1) == Terrain::High && py < 3) {
            c = shade(c, -22);
        } else if (at(0, -1) == Terrain::Cliff && py < 4) {
            c = shade(c, -26);
        }
        return c;
    }
    case Terrain::High: {
        Rgb c = shade(kHigh, cell_var);
        if (grain % 29u == 0u) {
            c = shade(c, -8);
        }
        if (at(0, -1) != Terrain::High && py < 2) {
            c = kHighRim;
        } else if (at(0, 1) != Terrain::High && py >= last - 2) {
            c = kHighFace; // the plateau's south face
        } else if ((at(-1, 0) != Terrain::High && px == 0) ||
                   (at(1, 0) != Terrain::High && px == last)) {
            c = shade(c, -18);
        }
        return c;
    }
    case Terrain::Rock: {
        Rgb c = shade(kRock, cell_var);
        if ((at(-1, 0) != Terrain::Rock && px == 0) || (at(1, 0) != Terrain::Rock && px == last) ||
            (at(0, -1) != Terrain::Rock && py == 0) || (at(0, 1) != Terrain::Rock && py == last)) {
            c = kRockEdge;
        } else if (px >= 2 && px <= 4 && py >= 2 && py <= 3) {
            c = shade(c, 28);
        } else if (grain % 5u == 0u) {
            c = shade(c, -10);
        }
        return c;
    }
    case Terrain::Cliff: {
        Rgb c = shade(kCliff, cell_var + static_cast<int>(grain % 9u) - 4);
        if (at(0, -1) != Terrain::Cliff && py < 3) {
            c = kCliffTop;
        } else if (at(0, 1) != Terrain::Cliff && py >= last - 1) {
            c = shade(c, -14);
        }
        return c;
    }
    }
    return kGround;
}

// The path: packed dirt with gravel and darker edges. The ramp up to a high-ground core is lighter,
// with step lines across it.
constexpr Rgb kPath{122, 98, 74};
constexpr Rgb kPathEdge{92, 72, 56};
constexpr Rgb kRamp{176, 142, 104};
constexpr Rgb kRampStep{138, 108, 78};

Rgb path_colour(const Map& m, int cx, int cy, int px, int py) {
    const bool ramp = m.path.at(cx, cy) == PathKind::Ramp;
    const auto edge = [&](int dx, int dy) {
        return !m.path.in_bounds(cx + dx, cy + dy) || m.path.at(cx + dx, cy + dy) == PathKind::None;
    };
    const int last = kCellPx - 1;
    const bool border = (edge(-1, 0) && px == 0) || (edge(1, 0) && px == last) ||
                        (edge(0, -1) && py == 0) || (edge(0, 1) && py == last);
    if (border) {
        return ramp ? kRampStep : kPathEdge;
    }
    if (ramp) {
        return py % 3 == 0 ? kRampStep : kRamp;
    }
    const std::uint32_t grain = mix(static_cast<std::uint32_t>(cx * kCellPx + px),
                                    static_cast<std::uint32_t>(cy * kCellPx + py));
    return grain % 11u == 0u ? shade(kPath, -14) : grain % 17u == 0u ? shade(kPath, 10) : kPath;
}

// The haze over unexplored low ground: colours pulled towards a cool grey, with a faint diagonal
// texture so it reads as haze rather than shadow.
constexpr Rgb kHaze{58, 62, 78};

Rgb haze(Rgb c, int x, int y) {
    const auto blend = [](int a, int b) { return (a * 55 + b * 45) / 100; };
    Rgb out{blend(c.r, kHaze.r), blend(c.g, kHaze.g), blend(c.b, kHaze.b)};
    if ((x + 2 * y) % 7 == 0) {
        out = shade(out, 7);
    }
    return out;
}

} // namespace

MapView::MapView(SDL_Renderer* renderer)
    : renderer_(renderer),
      pixels_(static_cast<std::size_t>(kMapPxW) * static_cast<std::size_t>(kMapPxH) * 4u, 0) {}

MapView::~MapView() {
    if (terrain_ != nullptr) {
        SDL_DestroyTexture(terrain_);
    }
}

bool MapView::set_map(const Map& map, const Fog& fog) {
    if (terrain_ == nullptr) {
        terrain_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                     kMapPxW, kMapPxH);
        if (terrain_ == nullptr) {
            SDL_Log("terrain texture failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureScaleMode(terrain_, SDL_SCALEMODE_NEAREST);
    }
    zone_ = map.zone;
    core_ = map.core;
    spawn_ = map.spawn;
    return update(map, fog, Rect{0, 0, kMapW, kMapH});
}

bool MapView::update(const Map& map, const Fog& fog, const Rect& cells) {
    if (terrain_ == nullptr || cells.w <= 0 || cells.h <= 0) {
        return terrain_ != nullptr;
    }
    for (int cy = cells.y; cy < cells.y + cells.h; ++cy) {
        for (int cx = cells.x; cx < cells.x + cells.w; ++cx) {
            // Once the core is placed, unexplored low ground is hazed, and path under the haze
            // is drawn as plain ground.
            const bool hazed = map.core.has_value() && !fog.explored({cx, cy}) &&
                               map.terrain.at(cx, cy) == Terrain::Ground;
            const bool path = map.path.at(cx, cy) != PathKind::None && !hazed;
            for (int py = 0; py < kCellPx; ++py) {
                const std::size_t row =
                    static_cast<std::size_t>(cy * kCellPx + py) * static_cast<std::size_t>(kMapPxW);
                for (int px = 0; px < kCellPx; ++px) {
                    Rgb c = path ? path_colour(map, cx, cy, px, py)
                                 : pixel_colour(map.terrain, cx, cy, px, py);
                    if (hazed) {
                        c = haze(c, cx * kCellPx + px, cy * kCellPx + py);
                    }
                    const std::size_t i = (row + static_cast<std::size_t>(cx * kCellPx + px)) * 4u;
                    pixels_[i + 0] = static_cast<std::uint8_t>(c.r);
                    pixels_[i + 1] = static_cast<std::uint8_t>(c.g);
                    pixels_[i + 2] = static_cast<std::uint8_t>(c.b);
                    pixels_[i + 3] = 255;
                }
            }
        }
    }
    const SDL_Rect area{cells.x * kCellPx, cells.y * kCellPx, cells.w * kCellPx, cells.h * kCellPx};
    const std::size_t first =
        (static_cast<std::size_t>(area.y) * static_cast<std::size_t>(kMapPxW) +
         static_cast<std::size_t>(area.x)) *
        4u;
    if (!SDL_UpdateTexture(terrain_, &area, pixels_.data() + first, kMapPxW * 4)) {
        SDL_Log("terrain upload failed: %s", SDL_GetError());
        return false;
    }
    return true;
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
    const auto world_rect = [&](int cx, int cy, int cw, int ch) {
        return SDL_FRect{static_cast<float>(cx * kCellPx - cam.x),
                         static_cast<float>(cy * kCellPx - cam.y), static_cast<float>(cw * kCellPx),
                         static_cast<float>(ch * kCellPx)};
    };

    if (!core_) {
        // The core zone, while the core hasn't been placed: a light tint and an outline.
        const SDL_FRect zone = world_rect(zone_.x, zone_.y, zone_.w, zone_.h);
        SDL_SetRenderDrawColor(renderer_, 90, 220, 255, 34);
        SDL_RenderFillRect(renderer_, &zone);
        SDL_SetRenderDrawColor(renderer_, 90, 220, 255, 230);
        SDL_RenderRect(renderer_, &zone);
        return;
    }

    // The core: a placeholder reactor (frame, body, glowing heart).
    const SDL_FRect frame = world_rect(core_->x, core_->y, kCoreSize, kCoreSize);
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

    // The spawn: a red marker where the path meets the map edge.
    const SDL_FRect spawn = world_rect(spawn_.x, spawn_.y, 2, 2);
    SDL_SetRenderDrawColor(renderer_, 220, 64, 52, 255);
    SDL_RenderFillRect(renderer_, &spawn);
    SDL_SetRenderDrawColor(renderer_, 255, 190, 120, 255);
    SDL_RenderRect(renderer_, &spawn);
}

void MapView::draw_ghost(CameraPos cam, Cell top_left, bool legal) const {
    const SDL_FRect ghost{static_cast<float>(top_left.x * kCellPx - cam.x),
                          static_cast<float>(top_left.y * kCellPx - cam.y),
                          static_cast<float>(kCoreSize * kCellPx),
                          static_cast<float>(kCoreSize * kCellPx)};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (legal) {
        SDL_SetRenderDrawColor(renderer_, 90, 230, 140, 90);
    } else {
        SDL_SetRenderDrawColor(renderer_, 240, 70, 60, 90);
    }
    SDL_RenderFillRect(renderer_, &ghost);
    if (legal) {
        SDL_SetRenderDrawColor(renderer_, 140, 255, 180, 240);
    } else {
        SDL_SetRenderDrawColor(renderer_, 255, 120, 100, 240);
    }
    SDL_RenderRect(renderer_, &ghost);
}

void MapView::draw_probe_preview(CameraPos cam, const Map& map, Cell top_left, bool legal,
                                 int range_px, Height height) const {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const PointPx centre = centre_of(top_left, kLightSize);
    // Cells in range that the probe can't see are shaded, so blocked sight lines show.
    const long long r2 = static_cast<long long>(range_px) * range_px;
    SDL_SetRenderDrawColor(renderer_, 10, 8, 20, 110);
    for (int cy = (centre.y - range_px) / kCellPx; cy <= (centre.y + range_px) / kCellPx; ++cy) {
        for (int cx = (centre.x - range_px) / kCellPx; cx <= (centre.x + range_px) / kCellPx;
             ++cx) {
            if (!map.terrain.in_bounds(cx, cy)) {
                continue;
            }
            const PointPx c = centre_of(Cell{cx, cy});
            const long long dx = c.x - centre.x;
            const long long dy = c.y - centre.y;
            if (dx * dx + dy * dy <= r2 && !can_see(map, centre, c, height)) {
                const SDL_FRect cell{static_cast<float>(cx * kCellPx - cam.x),
                                     static_cast<float>(cy * kCellPx - cam.y),
                                     static_cast<float>(kCellPx), static_cast<float>(kCellPx)};
                SDL_RenderFillRect(renderer_, &cell);
            }
        }
    }
    // The range circle (it includes the high-ground bonus).
    constexpr int kSegments = 72;
    std::array<SDL_FPoint, kSegments + 1> ring{};
    for (int i = 0; i <= kSegments; ++i) {
        const float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(kSegments);
        ring[static_cast<std::size_t>(i)] = {
            static_cast<float>(centre.x - cam.x) + static_cast<float>(range_px) * SDL_cosf(a),
            static_cast<float>(centre.y - cam.y) + static_cast<float>(range_px) * SDL_sinf(a)};
    }
    SDL_SetRenderDrawColor(renderer_, 250, 220, 120, 200);
    SDL_RenderLines(renderer_, ring.data(), static_cast<int>(ring.size()));
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
