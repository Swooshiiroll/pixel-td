#include "game/map_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/los.hpp"

namespace pixeltd::game {
namespace {

using Rgbf = std::array<float, 3>;

// Placeholder palette (temperate, #32) until the style board picks the art direction. Each ground
// type has a colour; trees darken their cell (so forests read zoomed out). Rock is the mountains'
// roof, shaded by its looks value; in the 3/4 view (#35) an open cell with rock directly north is
// covered by the mountain's cliff wall, and the cell south of that lies in the wall's shadow.
constexpr std::array<Rgbf, kGrounds> kGroundColour = {{
    {118.0f, 128.0f, 70.0f},  // soil (grassy)
    {92.0f, 118.0f, 58.0f},   // rich soil
    {200.0f, 182.0f, 128.0f}, // sand
    {140.0f, 136.0f, 126.0f}, // gravel
    {104.0f, 88.0f, 62.0f},   // mud
    {86.0f, 104.0f, 76.0f},   // marsh
    {86.0f, 140.0f, 160.0f},  // shallow water
    {44.0f, 82.0f, 128.0f},   // deep water
}};
constexpr Rgbf kRockColour{128.0f, 124.0f, 120.0f};
constexpr Rgbf kTreeColour{46.0f, 82.0f, 40.0f};
constexpr Rgbf kBareSoil{132.0f, 112.0f, 82.0f}; // soil without grass
constexpr Rgbf kWallColour{92.0f, 88.0f, 84.0f}; // a cell under a cliff wall, zoomed out
constexpr float kRoofLip = 1.15f;                // the roof's south edge, lit
constexpr float kWallShadow = 0.7f;              // the ground just south of a wall

// The fog layer: unexplored cells are pulled this far towards this colour.
constexpr std::uint8_t kHazeR = 58;
constexpr std::uint8_t kHazeG = 62;
constexpr std::uint8_t kHazeB = 80;
constexpr std::uint8_t kHazeAlpha = 115; // 0.45 * 255

// Sprites for the 3/4 view (#35, placeholders until the style board): one generated atlas of 16 px
// columns, drawn with nearest filtering for what's in view at 1/2 zoom and closer. Further out,
// the cell colours carry them. Trees stand on their cell (the foot at the bottom, the canopy
// rising over the cells behind); a cliff wall covers the open cell south of rock; a bank lip sits
// on the top edge of water below dry land, so the water lies lower than the ground.
constexpr int kCol = 16;                // an atlas column: one cell wide
constexpr int kAtlasH = 32;             // the tallest sprite (a tree)
constexpr int kTreeVariants = 3;        // columns 0-2: trees, 16 x 32
constexpr int kTreeShadowCol = 3;       // column 3: a tree's shadow, a soft ellipse 16 x 8
constexpr int kWallCol = 4;             // columns 4-7: cliff walls, 16 x 16
constexpr int kWallVariants = 4;        //
constexpr int kBankCol = 8;             // column 8: the bank lip, 16 x kBankPx
constexpr int kBankPx = 5;              //
constexpr int kWalkerCol = 9;           // column 9: a walker (#11), 16 x 16, a violet beetle
constexpr int kAtlasCols = 10;          //
constexpr float kSpritesFrom = 0.5f;    // the farthest zoom that draws them
constexpr int kWallEndPx = 2;           // the darker edge where a wall run ends
constexpr int kCoreHeightPx = 24;       // how tall the core stands
constexpr int kSpawnHeightPx = 16;      // how tall a spawn stands
constexpr int kBoxShadowPx = 6;         // the core's and spawns' shadow, to the south-east
constexpr int kLightBlockHeightPx = 12; // how tall the placeholder blocks stand (#11)
constexpr int kHeavyBlockHeightPx = 20; //
constexpr float kShotLiftPx = 10.0f;    // shots fly at about the turrets' height (#11 phase 2)

constexpr int block_height(BlockKind k) noexcept {
    return k == BlockKind::Light ? kLightBlockHeightPx : kHeavyBlockHeightPx;
}

// What a cell carries for the sprites.
constexpr std::uint8_t kMarkTree = 1;
constexpr std::uint8_t kMarkWall = 2;
constexpr std::uint8_t kMarkBank = 4;

// A small integer hash of a cell, for the sprites' variant, size and jitter.
constexpr std::uint32_t cell_hash(int x, int y) noexcept {
    std::uint32_t h =
        static_cast<std::uint32_t>(x) * 0x8da6b343u ^ static_cast<std::uint32_t>(y) * 0xd8163841u;
    h ^= h >> 13u;
    h *= 0x2c1b3c6du;
    h ^= h >> 15u;
    return h;
}

constexpr std::size_t cell_index(int x, int y) noexcept {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(kMapW) +
           static_cast<std::size_t>(x);
}

constexpr std::size_t cell_bytes() noexcept {
    return static_cast<std::size_t>(kMapW) * static_cast<std::size_t>(kMapH) * 4u;
}

// Where a cell's tree stands: its foot (world pixels), its width (13-17 px; it's twice as tall)
// and its variant, from the cell's hash, so trees don't sit on a grid. The foot stays in the cell.
struct TreeAt {
    int foot_x = 0;
    int foot_y = 0;
    int size = 0;
    int variant = 0;
};

constexpr TreeAt tree_at(int x, int y) noexcept {
    const std::uint32_t h = cell_hash(x, y);
    return {x * kCellPx + kCellPx / 2 + static_cast<int>((h >> 4u) % 7u) - 3,
            (y + 1) * kCellPx - 1 - static_cast<int>((h >> 8u) % 3u), 13 + static_cast<int>(h % 5u),
            static_cast<int>((h >> 12u) % kTreeVariants)};
}

constexpr SDL_FRect atlas(int col, int w, int h) noexcept {
    return {static_cast<float>(col * kCol), 0.0f, static_cast<float>(w), static_cast<float>(h)};
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
    if (sprites_ != nullptr) {
        SDL_DestroyTexture(sprites_);
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
    return create_sprites();
}

bool MapView::create_sprites() {
    constexpr int kW = kCol * kAtlasCols;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kW) * kAtlasH * 4, 0);
    const auto put = [&](int x, int y, float r, float g, float b, int a) {
        std::uint8_t* p =
            px.data() + (static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)) * 4u;
        p[0] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 255.0f));
        p[1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 255.0f));
        p[2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 255.0f));
        p[3] = static_cast<std::uint8_t>(a);
    };

    // Trees: a trunk 3 px wide at the foot, under a lumpy canopy (its radius wobbles by angle; a
    // little taller than wide) in greens, lit from the north-west, with a dark rim.
    for (int v = 0; v < kTreeVariants; ++v) {
        const int ox = kCol * v;
        for (int y = 20; y < kAtlasH; ++y) {
            for (int x = 6; x <= 8; ++x) {
                const float k = (x == 6   ? 1.2f
                                 : x == 8 ? 0.75f
                                          : 1.0f) *
                                (y == kAtlasH - 1 ? 0.7f : 1.0f);
                put(ox + x, y, 92.0f * k, 64.0f * k, 42.0f * k, 255);
            }
        }
        const float cx = 7.5f;
        const float cy = 15.0f;
        for (int y = 0; y < kAtlasH; ++y) {
            for (int x = 0; x < kCol; ++x) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = (static_cast<float>(y) - cy) / 1.25f;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float angle = std::atan2(dy, dx);
                const float radius = 6.8f + 0.9f * std::sin(angle * static_cast<float>(3 + v) +
                                                            static_cast<float>(v));
                if (d > radius) {
                    continue;
                }
                const std::uint32_t h = cell_hash(x + 31 * v, y);
                const float lit = std::clamp(1.0f - (dx + dy) * 0.05f, 0.65f, 1.3f);
                const float speck = 0.88f + static_cast<float>(h % 25u) / 100.0f;
                const bool rim = d > radius - 1.0f;
                const float k = (rim ? 0.6f : 1.0f) * lit * speck;
                put(ox + x, y, 52.0f * k, (96.0f + 8.0f * static_cast<float>(v)) * k, 44.0f * k,
                    255);
            }
        }
    }

    // A tree's shadow: a soft dark ellipse, laid on the ground to the south-east of its foot.
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < kCol; ++x) {
            const float dx = (static_cast<float>(x) - 7.5f) / 7.5f;
            const float dy = (static_cast<float>(y) - 3.5f) / 3.5f;
            const float n = dx * dx + dy * dy;
            if (n <= 1.0f) {
                put(kCol * kTreeShadowCol + x, y, 12.0f, 18.0f, 10.0f, n <= 0.5f ? 110 : 60);
            }
        }
    }

    // Cliff walls: rock strata in bands, with a dark seam between them, lit at the top edge (the
    // roof's lip) and darker towards the foot, flecked, with a crack picked by the variant.
    for (int v = 0; v < kWallVariants; ++v) {
        const int ox = kCol * (kWallCol + v);
        for (int y = 0; y < kCol; ++y) {
            for (int x = 0; x < kCol; ++x) {
                const std::uint32_t h = cell_hash(x + 17 * v, y + 40);
                float k = (1.08f - 0.4f * static_cast<float>(y) / 15.0f) *
                          (((y + v) / 4) % 2 == 0 ? 1.0f : 0.9f) *
                          (0.92f + static_cast<float>(h % 17u) / 100.0f);
                if ((y + v) % 4 == 0 && y > 1) {
                    k *= 0.82f; // the seam between strata
                }
                if (y == 0) {
                    k *= 1.3f;
                } else if (y >= kCol - 2) {
                    k *= 0.75f;
                }
                put(ox + x, y, 104.0f * k, 98.0f * k, 92.0f * k, 255);
            }
        }
        const int crack_x = 3 + static_cast<int>(cell_hash(v, 99) % 10u);
        for (int y = 3; y < 9 + v % 3; ++y) {
            const int x = std::clamp(crack_x + static_cast<int>((cell_hash(v, y) >> 3u) % 3u) - 1,
                                     0, kCol - 1);
            put(ox + x, y, 44.0f, 42.0f, 40.0f, 255);
        }
    }

    // The bank lip: earth, light at the top and dark at the drop, then a shadow on the water.
    constexpr std::array<std::array<int, 4>, kBankPx> kBank = {{
        {150, 128, 92, 255},
        {126, 104, 74, 255},
        {104, 84, 60, 255},
        {70, 56, 42, 230},
        {20, 30, 40, 110},
    }};
    for (int y = 0; y < kBankPx; ++y) {
        for (int x = 0; x < kCol; ++x) {
            const float k = 0.92f + static_cast<float>(cell_hash(x, y + 70) % 17u) / 100.0f;
            const auto& c = kBank[static_cast<std::size_t>(y)];
            put(kCol * kBankCol + x, y, static_cast<float>(c[0]) * k, static_cast<float>(c[1]) * k,
                static_cast<float>(c[2]) * k, c[3]);
        }
    }

    // A walker (#11, placeholder): a violet beetle seen from the 3/4 angle, its body a lit oval
    // with a dark rim, a head to the north with two hot eyes, and three legs a side.
    for (int y = 0; y < kCol; ++y) {
        for (int x = 0; x < kCol; ++x) {
            const float fx = static_cast<float>(x) - 7.5f;
            const float body =
                (fx / 5.5f) * (fx / 5.5f) +
                ((static_cast<float>(y) - 9.5f) / 4.8f) * ((static_cast<float>(y) - 9.5f) / 4.8f);
            const float head =
                (fx / 3.0f) * (fx / 3.0f) +
                ((static_cast<float>(y) - 4.0f) / 2.6f) * ((static_cast<float>(y) - 4.0f) / 2.6f);
            const int ox = kCol * kWalkerCol + x;
            if (body <= 1.0f) {
                const float lit =
                    std::clamp(1.15f - (fx + static_cast<float>(y) - 9.5f) * 0.05f, 0.7f, 1.3f);
                const float k = (body > 0.7f ? 0.55f : 1.0f) * lit;
                put(ox, y, 92.0f * k, 54.0f * k, 124.0f * k, 255);
            } else if (head <= 1.0f) {
                put(ox, y, 46.0f, 28.0f, 64.0f, 255);
            } else if ((y == 8 || y == 10 || y == 12) && (x == 1 || x == 14)) {
                put(ox, y, 30.0f, 18.0f, 40.0f, 255); // legs
            }
        }
    }
    put(kCol * kWalkerCol + 6, 3, 255.0f, 120.0f, 90.0f, 255); // eyes
    put(kCol * kWalkerCol + 9, 3, 255.0f, 120.0f, 90.0f, 255);

    sprites_ =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kW, kAtlasH);
    if (sprites_ == nullptr || !SDL_UpdateTexture(sprites_, nullptr, px.data(), kW * 4)) {
        SDL_Log("sprites failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(sprites_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(sprites_, SDL_SCALEMODE_NEAREST);
    return true;
}

bool MapView::set_map(const Map& map, const GenLayers& layers, const Fog& fog) {
    const Uint64 start = SDL_GetTicksNS();
    if (terrain_ == nullptr && !create_textures()) {
        return false;
    }
    core_ = map.core;
    spawns_ = map.spawns;
    marks_.assign(static_cast<std::size_t>(kMapW) * static_cast<std::size_t>(kMapH), 0);
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            std::uint8_t m = 0;
            const bool open = map.rock.at(x, y) == 0;
            const bool rock_north = y > 0 && map.rock.at(x, y - 1) != 0;
            if (map.plant.at(x, y) == Plant::Tree) {
                m |= kMarkTree;
            }
            if (open && rock_north) {
                m |= kMarkWall;
            }
            if (open && y > 0 && !rock_north && is_water(map.ground.at(x, y)) &&
                !is_water(map.ground.at(x, y - 1))) {
                m |= kMarkBank;
            }
            marks_[cell_index(x, y)] = m;
        }
    }
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
        const Grid<int>* field = overlay_ == Overlay::Mountain    ? &layers.mountain
                                 : overlay_ == Overlay::Fertility ? &layers.fertility
                                                                  : &layers.moisture;
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

    // The terrain. The ground's colour (soil shows bare where nothing grows), softened over its
    // neighbours, darkened under trees. Rock is the roof, gently shaded by its 1..255 looks value
    // and lit along its south edge; the open cell below it is covered by the cliff wall, and the
    // one below that is in the wall's shadow (3/4 view, #35).
    const auto rock = [&](int x, int y) {
        return map.rock.in_bounds(x, y) ? static_cast<float>(map.rock.at(x, y)) : 0.0f;
    };
    const auto rock_at = [&](int x, int y) { return rock(x, y) > 0.0f; };
    const auto wall_at = [&](int x, int y) {
        return map.rock.in_bounds(x, y) && !rock_at(x, y) && rock_at(x, y - 1);
    };
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            Rgbf base{0.0f, 0.0f, 0.0f};
            float k = 0.86f;
            if (rock_at(x, y)) {
                base = kRockColour;
                const float gx = rock(x + 1, y) - rock(x - 1, y);
                const float gy = rock(x, y + 1) - rock(x, y - 1);
                k = (1.0f + 0.25f * rock(x, y) / 255.0f) *
                    std::clamp(1.0f - (gx + gy) * 0.0025f, 0.75f, 1.25f);
                if (y + 1 < kMapH && !rock_at(x, y + 1)) {
                    k *= kRoofLip;
                }
            } else if (wall_at(x, y)) {
                base = kWallColour;
                k = 1.0f;
            } else {
                // A light blend with the 4 neighbours softens the edges between ground types.
                const auto colour_at = [&](int cx, int cy) -> Rgbf {
                    const int qx = std::clamp(cx, 0, kMapW - 1);
                    const int qy = std::clamp(cy, 0, kMapH - 1);
                    const Ground g = map.ground.at(qx, qy);
                    if ((g == Ground::Soil || g == Ground::RichSoil) &&
                        map.plant.at(qx, qy) == Plant::None) {
                        return kBareSoil;
                    }
                    return kGroundColour[static_cast<std::size_t>(g)];
                };
                const Rgbf c0 = colour_at(x, y);
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    base[ch] = c0[ch] * 0.6f;
                }
                for (const auto& [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    const Rgbf cn = colour_at(x + dx, y + dy);
                    for (std::size_t ch = 0; ch < 3; ++ch) {
                        base[ch] += cn[ch] * 0.1f;
                    }
                }
                if (map.plant.at(x, y) == Plant::Tree) {
                    for (std::size_t ch = 0; ch < 3; ++ch) {
                        base[ch] = base[ch] * 0.35f + kTreeColour[ch] * 0.65f;
                    }
                }
            }
            if (wall_at(x, y - 1)) {
                k *= kWallShadow;
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

void MapView::draw(const DrawXf& xf, ViewSize view, const Fog& fog, const Sim& sim) const {
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

    // The cells in view, with a margin: a column each side (trees are jittered) and two rows below
    // (trees and the core rise over the cells behind them).
    const int x0 = std::max(0, static_cast<int>(sx) / kCellPx - 1);
    const int y0 = std::max(0, static_cast<int>(sy) / kCellPx - 1);
    const int x1 = std::min(kMapW - 1, static_cast<int>(sx + sw) / kCellPx + 1);
    const int y1 = std::min(kMapH - 1, static_cast<int>(sy + sh) / kCellPx + 2);
    const Rect span{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    const bool sprites =
        overlay_ == Overlay::Off && xf.k >= kSpritesFrom && sprites_ != nullptr && !marks_.empty();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (sprites) {
        draw_ground(xf, span);
    }
    draw_standing(xf, span, sprites, fog, sim);
    draw_shots(xf, sim);

    // Fog: one texel per cell, stretched with linear filtering.
    if (fog_shown_ && core_) {
        SDL_RenderTexture(renderer_, fog_, &src, &dst);
    }

    // The spawns' labels: the wave each opens at, over the fog.
    for (const Spawn& s : spawns_) {
        if (fog_shown_ && !fog.spawn_found(s)) {
            continue;
        }
        const SDL_FRect r = xf.rect(spawn_top(s));
        char label[8];
        std::snprintf(label, sizeof label, "%d", s.opens_at_wave);
        SDL_SetRenderDrawColor(renderer_, 255, 240, 220, 255);
        SDL_RenderDebugText(renderer_, r.x + r.w + 2.0f, r.y + r.h / 2.0f - 4.0f, label);
    }
}

RectPx MapView::spawn_top(const Spawn& s) {
    return {s.cell.x * kCellPx, s.cell.y * kCellPx - kSpawnHeightPx, 2 * kCellPx, 2 * kCellPx};
}

void MapView::draw_ground(const DrawXf& xf, const Rect& span) const {
    // What lies flat on the ground: the banks' lips, then the trees' shadows.
    const SDL_FRect bank = atlas(kBankCol, kCol, kBankPx);
    const SDL_FRect shadow = atlas(kTreeShadowCol, kCol, 8);
    for (int y = span.y; y < span.y + span.h; ++y) {
        for (int x = span.x; x < span.x + span.w; ++x) {
            const std::uint8_t m = marks_[cell_index(x, y)];
            if ((m & kMarkBank) != 0) {
                const SDL_FRect d = xf.rect(RectPx{x * kCellPx, y * kCellPx, kCellPx, kBankPx});
                SDL_RenderTexture(renderer_, sprites_, &bank, &d);
            }
            if ((m & kMarkTree) != 0) {
                const TreeAt t = tree_at(x, y);
                const SDL_FRect d = xf.rect(RectPx{t.foot_x - t.size / 2 + 5,
                                                   t.foot_y - t.size / 4 + 2, t.size, t.size / 2});
                SDL_RenderTexture(renderer_, sprites_, &shadow, &d);
            }
        }
    }
}

void MapView::draw_standing(const DrawXf& xf, const Rect& span, bool sprites, const Fog& fog,
                            const Sim& sim) const {
    // Painter's order by row, north to south, so whatever stands nearer the viewer covers what
    // stands behind it: in each row the cliff walls, then the trees whose foot is in it, then the
    // spawns, blocks and walkers, then the core when it's its foot row.
    const int core_row =
        core_ ? (rect_of(*core_, kCoreSub).y + kCoreSub * kSubPx - 1) / kCellPx : -1;

    // The blocks and the visible walkers in view, sorted into their foot rows.
    standing_.clear();
    const auto in_span = [&](int cx0, int cx1, int row) {
        return row >= span.y && row < span.y + span.h && cx1 >= span.x && cx0 < span.x + span.w;
    };
    for (std::size_t i = 0; i < sim.blocks().size(); ++i) {
        const Block& b = sim.blocks()[i];
        if (!b.alive) {
            continue;
        }
        const RectPx r = rect_of(b.top_left, b.size_sub());
        const int row = (r.y + r.h - 1) / kCellPx;
        if (in_span(r.x / kCellPx, (r.x + r.w - 1) / kCellPx, row)) {
            standing_.push_back({row, 0, static_cast<int>(i)});
        }
    }
    for (std::size_t i = 0; i < sim.walkers().size(); ++i) {
        const PointPx c = sim.walkers()[i].centre();
        const Cell at{c.x / kCellPx, c.y / kCellPx};
        if (fog_shown_ && core_ && !fog.explored(at)) {
            continue; // hidden on unexplored ground
        }
        const int row = (c.y + kSubPx - 1) / kCellPx;
        if (in_span(at.x, at.x, row)) {
            standing_.push_back({row, 1, static_cast<int>(i)});
        }
    }
    std::sort(standing_.begin(), standing_.end(), [](const Standing& a, const Standing& b) {
        return a.row != b.row     ? a.row < b.row
               : a.kind != b.kind ? a.kind < b.kind
                                  : a.index < b.index;
    });
    std::size_t next_standing = 0;

    for (int y = span.y; y < span.y + span.h; ++y) {
        if (sprites) {
            for (int x = span.x; x < span.x + span.w; ++x) {
                if ((marks_[cell_index(x, y)] & kMarkWall) == 0) {
                    continue;
                }
                const std::uint32_t h = cell_hash(x, y);
                const SDL_FRect s =
                    atlas(kWallCol + static_cast<int>(h % kWallVariants), kCol, kCol);
                const SDL_FRect d = xf.rect(RectPx{x * kCellPx, y * kCellPx, kCellPx, kCellPx});
                SDL_RenderTexture(renderer_, sprites_, &s, &d);
                // Where the run of wall ends, a darker edge.
                SDL_SetRenderDrawColor(renderer_, 20, 18, 16, 90);
                if (x == 0 || (marks_[cell_index(x - 1, y)] & kMarkWall) == 0) {
                    const SDL_FRect e =
                        xf.rect(RectPx{x * kCellPx, y * kCellPx, kWallEndPx, kCellPx});
                    SDL_RenderFillRect(renderer_, &e);
                }
                if (x == kMapW - 1 || (marks_[cell_index(x + 1, y)] & kMarkWall) == 0) {
                    const SDL_FRect e = xf.rect(
                        RectPx{(x + 1) * kCellPx - kWallEndPx, y * kCellPx, kWallEndPx, kCellPx});
                    SDL_RenderFillRect(renderer_, &e);
                }
            }
            for (int x = span.x; x < span.x + span.w; ++x) {
                if ((marks_[cell_index(x, y)] & kMarkTree) == 0) {
                    continue;
                }
                const TreeAt t = tree_at(x, y);
                const SDL_FRect s = atlas(t.variant, kCol, kAtlasH);
                const SDL_FRect d = xf.rect(
                    RectPx{t.foot_x - t.size / 2, t.foot_y - 2 * t.size + 1, t.size, 2 * t.size});
                SDL_RenderTexture(renderer_, sprites_, &s, &d);
            }
        }
        // Spawns before the walkers in their row, so walkers coming out stand in front of them.
        for (const Spawn& s : spawns_) {
            if (s.cell.y + 1 == y && (!fog_shown_ || fog.spawn_found(s))) {
                draw_spawn(xf, s);
            }
        }
        for (; next_standing < standing_.size() && standing_[next_standing].row == y;
             ++next_standing) {
            const Standing& st = standing_[next_standing];
            const auto i = static_cast<std::size_t>(st.index);
            if (st.kind == 0) {
                draw_block(xf, sim.blocks()[i]);
            } else {
                draw_walker(xf, sim.walkers()[i]);
            }
        }
        if (y == core_row) {
            draw_core(xf);
        }
    }
}

void MapView::draw_core(const DrawXf& xf) const {
    // A placeholder reactor standing on its footprint: a shadow to the south-east, the front face
    // rising from the footprint's south edge (with a glowing slit), and the top (frame, body,
    // glowing heart) raised by its height.
    const RectPx foot = rect_of(*core_, kCoreSub);
    const SDL_FRect shade =
        xf.rect(RectPx{foot.x + kBoxShadowPx, foot.y + kBoxShadowPx, foot.w, foot.h});
    SDL_SetRenderDrawColor(renderer_, 10, 14, 10, 70);
    SDL_RenderFillRect(renderer_, &shade);

    const SDL_FRect front =
        xf.rect(RectPx{foot.x, foot.y + foot.h - kCoreHeightPx, foot.w, kCoreHeightPx});
    SDL_SetRenderDrawColor(renderer_, 40, 52, 74, 255);
    SDL_RenderFillRect(renderer_, &front);
    const SDL_FRect slit{front.x + 24.0f * xf.k, front.y + 9.0f * xf.k, front.w - 48.0f * xf.k,
                         6.0f * xf.k};
    SDL_SetRenderDrawColor(renderer_, 110, 230, 255, 255);
    SDL_RenderFillRect(renderer_, &slit);
    SDL_SetRenderDrawColor(renderer_, 90, 130, 160, 255);
    SDL_RenderRect(renderer_, &front);

    const SDL_FRect frame = xf.rect(RectPx{foot.x, foot.y - kCoreHeightPx, foot.w, foot.h});
    SDL_SetRenderDrawColor(renderer_, 28, 34, 52, 255);
    SDL_RenderFillRect(renderer_, &frame);
    const float b = 4.0f * xf.k;
    const SDL_FRect body{frame.x + b, frame.y + b, frame.w - 2.0f * b, frame.h - 2.0f * b};
    SDL_SetRenderDrawColor(renderer_, 70, 96, 128, 255);
    SDL_RenderFillRect(renderer_, &body);
    const float hrt = 24.0f * xf.k;
    const SDL_FRect heart{frame.x + hrt, frame.y + hrt, frame.w - 2.0f * hrt, frame.h - 2.0f * hrt};
    SDL_SetRenderDrawColor(renderer_, 110, 230, 255, 255);
    SDL_RenderFillRect(renderer_, &heart);
    SDL_SetRenderDrawColor(renderer_, 140, 200, 230, 255);
    SDL_RenderRect(renderer_, &frame);
}

void MapView::draw_spawn(const DrawXf& xf, const Spawn& s) const {
    // A red placeholder gate standing on its 2 x 2 cells: a shadow, the front face with a dark
    // opening, and the top raised by its height.
    const RectPx foot{s.cell.x * kCellPx, s.cell.y * kCellPx, 2 * kCellPx, 2 * kCellPx};
    const SDL_FRect shade =
        xf.rect(RectPx{foot.x + kBoxShadowPx, foot.y + kBoxShadowPx, foot.w, foot.h});
    SDL_SetRenderDrawColor(renderer_, 10, 14, 10, 70);
    SDL_RenderFillRect(renderer_, &shade);

    const SDL_FRect front =
        xf.rect(RectPx{foot.x, foot.y + foot.h - kSpawnHeightPx, foot.w, kSpawnHeightPx});
    SDL_SetRenderDrawColor(renderer_, 150, 40, 34, 255);
    SDL_RenderFillRect(renderer_, &front);
    const SDL_FRect gate{front.x + 9.0f * xf.k, front.y + 3.0f * xf.k, front.w - 18.0f * xf.k,
                         front.h - 3.0f * xf.k};
    SDL_SetRenderDrawColor(renderer_, 40, 12, 18, 255);
    SDL_RenderFillRect(renderer_, &gate);

    const SDL_FRect top = xf.rect(spawn_top(s));
    SDL_SetRenderDrawColor(renderer_, 220, 64, 52, 255);
    SDL_RenderFillRect(renderer_, &top);
    SDL_SetRenderDrawColor(renderer_, 255, 190, 120, 255);
    SDL_RenderRect(renderer_, &top);
}

void MapView::draw_health(const DrawXf& xf, int x, int y, int w, int health, int max_health) const {
    if (max_health <= 0 || health >= max_health) {
        return;
    }
    const int filled = std::clamp(w * std::max(0, health) / max_health, 0, w);
    const SDL_FRect back = xf.rect(RectPx{x - 1, y - 1, w + 2, 4});
    SDL_SetRenderDrawColor(renderer_, 16, 12, 12, 200);
    SDL_RenderFillRect(renderer_, &back);
    const SDL_FRect bar = xf.rect(RectPx{x, y, filled, 2});
    if (health * 3 > max_health) {
        SDL_SetRenderDrawColor(renderer_, 120, 230, 110, 255);
    } else {
        SDL_SetRenderDrawColor(renderer_, 240, 90, 60, 255);
    }
    SDL_RenderFillRect(renderer_, &bar);
}

void MapView::draw_block(const DrawXf& xf, const Block& b) const {
    // A placeholder block standing on its footprint, like the core: a shadow, the front face, and
    // the top raised by its height; a health bar once it's damaged. A test tower (#11 phase 2) is
    // a teal block with a turret on top, its barrel towards where it last aimed.
    const RectPx foot = rect_of(b.top_left, b.size_sub());
    const int h = block_height(b.kind);
    const SDL_FRect shade = xf.rect(RectPx{foot.x + 4, foot.y + 4, foot.w, foot.h});
    SDL_SetRenderDrawColor(renderer_, 10, 14, 10, 70);
    SDL_RenderFillRect(renderer_, &shade);
    const SDL_FRect front = xf.rect(RectPx{foot.x, foot.y + foot.h - h, foot.w, h});
    if (b.armed) {
        SDL_SetRenderDrawColor(renderer_, 46, 82, 90, 255);
    } else {
        SDL_SetRenderDrawColor(renderer_, 84, 88, 96, 255);
    }
    SDL_RenderFillRect(renderer_, &front);
    const SDL_FRect top = xf.rect(RectPx{foot.x, foot.y - h, foot.w, foot.h});
    if (b.armed) {
        SDL_SetRenderDrawColor(renderer_, 70, 124, 132, 255);
    } else {
        SDL_SetRenderDrawColor(renderer_, 128, 134, 142, 255);
    }
    SDL_RenderFillRect(renderer_, &top);
    const SDL_FRect inner = xf.rect(RectPx{foot.x + 3, foot.y - h + 3, foot.w - 6, foot.h - 6});
    if (b.armed) {
        SDL_SetRenderDrawColor(renderer_, 92, 152, 160, 255);
    } else {
        SDL_SetRenderDrawColor(renderer_, 150, 156, 164, 255);
    }
    SDL_RenderFillRect(renderer_, &inner);
    SDL_SetRenderDrawColor(renderer_, 40, 44, 50, 255);
    SDL_RenderRect(renderer_, &top);
    SDL_RenderRect(renderer_, &front);
    if (b.armed) {
        draw_turret(xf, b, foot, h);
    }
    draw_health(xf, foot.x + 3, foot.y - h + 2, foot.w - 6, b.health(), b.max_health);
}

void MapView::draw_walker(const DrawXf& xf, const Walker& w) const {
    // The beetle stands on its 16 px square: a small shadow, the sprite rising a little over the
    // square behind, and a health bar over it once it's hurt.
    const PointPx c = w.centre();
    if (sprites_ != nullptr) {
        const SDL_FRect shadow_src = atlas(kTreeShadowCol, kCol, 8);
        const SDL_FRect shadow = xf.rect(RectPx{c.x - 5, c.y + 3, 12, 5});
        SDL_RenderTexture(renderer_, sprites_, &shadow_src, &shadow);
        const SDL_FRect src = atlas(kWalkerCol, kCol, kCol);
        const SDL_FRect d = xf.rect(RectPx{c.x - 8, c.y - 10, kCol, kCol});
        SDL_RenderTexture(renderer_, sprites_, &src, &d);
    }
    if (w.state == WalkerState::Breaking) {
        // A spark where it bites.
        SDL_SetRenderDrawColor(renderer_, 255, 210, 120, 230);
        const SDL_FRect spark = xf.rect(RectPx{c.x + 3, c.y - 12, 2, 2});
        SDL_RenderFillRect(renderer_, &spark);
    }
    draw_health(xf, c.x - 6, c.y - 13, 12, w.health, w.max_health);
}

void MapView::draw_block_preview(const DrawXf& xf, SubCell top_left, BlockKind kind,
                                 bool legal) const {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const RectPx foot = rect_of(top_left, block_size_sub(kind));
    const SDL_FRect r = xf.rect(foot);
    if (legal) {
        SDL_SetRenderDrawColor(renderer_, 120, 230, 140, 110);
    } else {
        SDL_SetRenderDrawColor(renderer_, 240, 80, 60, 130);
    }
    SDL_RenderFillRect(renderer_, &r);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 220);
    SDL_RenderRect(renderer_, &r);
}

void MapView::draw_routes(const DrawXf& xf, const Fog& fog, const Sim& sim) const {
    // Recomputed only when the sim has rebuilt its routes.
    if (routes_build_ != sim.route_builds() || routes_.size() != spawns_.size()) {
        routes_.resize(spawns_.size());
        for (std::size_t i = 0; i < spawns_.size(); ++i) {
            ShownRoute& sr = routes_[i];
            sr.route = sim.route_from(spawns_[i]);
            sr.length_px = 0.0f;
            for (std::size_t n = 1; n < sr.route.nodes.size(); ++n) {
                const SubCell a = sr.route.nodes[n - 1];
                const SubCell b = sr.route.nodes[n];
                sr.length_px += (a.x != b.x && a.y != b.y) ? 11.3137f : 8.0f;
            }
        }
        routes_build_ = sim.route_builds();
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (std::size_t i = 0; i < spawns_.size(); ++i) {
        if (fog_shown_ && !fog.spawn_found(spawns_[i])) {
            continue;
        }
        const ShownRoute& sr = routes_[i];
        route_points_.clear();
        for (const SubCell n : sr.route.nodes) {
            const PointPx p = node_centre(n);
            route_points_.push_back({xf.x(static_cast<float>(p.x)), xf.y(static_cast<float>(p.y))});
        }
        if (sr.route.sealed) {
            SDL_SetRenderDrawColor(renderer_, 255, 150, 60, 230);
        } else {
            SDL_SetRenderDrawColor(renderer_, 110, 220, 255, 220);
        }
        if (route_points_.size() >= 2) {
            SDL_RenderLines(renderer_, route_points_.data(),
                            static_cast<int>(route_points_.size()));
        }
        // The breach marker: the block this spawn's walkers will attack.
        if (sr.route.sealed && sr.route.breach_block >= 0 &&
            static_cast<std::size_t>(sr.route.breach_block) < sim.blocks().size()) {
            const Block& b = sim.blocks()[static_cast<std::size_t>(sr.route.breach_block)];
            if (b.alive) {
                const RectPx foot = rect_of(b.top_left, b.size_sub());
                const int h = block_height(b.kind);
                SDL_SetRenderDrawColor(renderer_, 255, 90, 40, 255);
                for (int g = 1; g <= 2; ++g) {
                    const SDL_FRect m = xf.rect(
                        RectPx{foot.x - g, foot.y - h - g, foot.w + 2 * g, foot.h + h + 2 * g});
                    SDL_RenderRect(renderer_, &m);
                }
            }
        }
        // Its length and walk time, under the spawn.
        char label[64];
        const double seconds = static_cast<double>(sim.cost_ms(sr.route.cost)) / 1000.0;
        const char* pending = sim.routes_pending() || sim.routes_dirty() ? " (pending)" : "";
        if (sr.route.nodes.empty()) {
            std::snprintf(label, sizeof label, "no route");
        } else if (sr.route.sealed) {
            std::snprintf(label, sizeof label, "SEALED: breach %.0f cells %.0f s%s",
                          static_cast<double>(sr.length_px) / kCellPx, seconds, pending);
        } else {
            std::snprintf(label, sizeof label, "route %.0f cells %.0f s%s",
                          static_cast<double>(sr.length_px) / kCellPx, seconds, pending);
        }
        const Spawn& s = spawns_[i];
        SDL_SetRenderDrawColor(renderer_, 255, 240, 220, 255);
        SDL_RenderDebugText(renderer_, xf.x(static_cast<float>(s.cell.x * kCellPx)),
                            xf.y(static_cast<float>((s.cell.y + 2) * kCellPx + 4)), label);
    }
}

void MapView::draw_turret(const DrawXf& xf, const Block& b, const RectPx& foot, int h) const {
    // A dark square turret in the middle of the top, and a barrel towards its last aim (north
    // until it has fired).
    const int size = b.kind == BlockKind::Light ? 10 : 18;
    const int barrel = b.kind == BlockKind::Light ? 9 : 15;
    const int cx = foot.x + foot.w / 2;
    const int cy = foot.y - h + foot.h / 2;
    const PointPx c = centre_of(b.top_left, b.size_sub());
    float dx = 0.0f;
    float dy = -1.0f;
    if (b.gun.shots > 0) {
        const auto ax = static_cast<float>(b.gun.aim.x - c.x);
        const auto ay = static_cast<float>(b.gun.aim.y - c.y);
        const float len = std::sqrt(ax * ax + ay * ay);
        if (len > 0.5f) {
            dx = ax / len;
            dy = ay / len;
        }
    }
    const auto fx = static_cast<float>(cx);
    const auto fy = static_cast<float>(cy);
    const auto fb = static_cast<float>(barrel);
    SDL_SetRenderDrawColor(renderer_, 200, 236, 240, 255);
    for (int t = -1; t <= 1; ++t) {
        // Three lines side by side: a barrel about 2 px thick at any zoom step.
        const float ox = -dy * static_cast<float>(t) * 0.6f;
        const float oy = dx * static_cast<float>(t) * 0.6f;
        SDL_RenderLine(renderer_, xf.x(fx + ox), xf.y(fy + oy), xf.x(fx + ox + dx * fb),
                       xf.y(fy + oy + dy * fb));
    }
    const SDL_FRect body = xf.rect(RectPx{cx - size / 2, cy - size / 2, size, size});
    SDL_SetRenderDrawColor(renderer_, 30, 46, 54, 255);
    SDL_RenderFillRect(renderer_, &body);
    const SDL_FRect lens = xf.rect(RectPx{cx - 1, cy - 1, 3, 3});
    SDL_SetRenderDrawColor(renderer_, 140, 255, 230, 255);
    SDL_RenderFillRect(renderer_, &lens);
}

void MapView::draw_shots(const DrawXf& xf, const Sim& sim) const {
    // Each shot in flight: a 2 x 2 spark with a tick's trail behind it, lifted to the turrets'
    // height so it reads as flying.
    for (const Projectile& p : sim.projectiles()) {
        if (!p.alive) {
            continue;
        }
        const float x = static_cast<float>(p.x) / static_cast<float>(kFix);
        const float y = static_cast<float>(p.y) / static_cast<float>(kFix) - kShotLiftPx;
        const float tx = x - static_cast<float>(p.vx) / static_cast<float>(kFix);
        const float ty = y - static_cast<float>(p.vy) / static_cast<float>(kFix);
        SDL_SetRenderDrawColor(renderer_, 255, 190, 90, 170);
        SDL_RenderLine(renderer_, xf.x(tx), xf.y(ty), xf.x(x), xf.y(y));
        const SDL_FRect spark{xf.x(x - 1.0f), xf.y(y - 1.0f), 2.0f * xf.k, 2.0f * xf.k};
        SDL_SetRenderDrawColor(renderer_, 255, 245, 170, 255);
        SDL_RenderFillRect(renderer_, &spark);
    }
}

void MapView::draw_reach(const DrawXf& xf, const Map& map, PointPx centre, int range_px) const {
    // Within the reach (a circle): cells in sight get a light tint; cells hidden behind rock are
    // shaded, so blocked sight lines show.
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const long long reach2 = static_cast<long long>(range_px) * range_px;
    for (int cy = (centre.y - range_px) / kCellPx; cy <= (centre.y + range_px) / kCellPx; ++cy) {
        for (int cx = (centre.x - range_px) / kCellPx; cx <= (centre.x + range_px) / kCellPx;
             ++cx) {
            if (!map.rock.in_bounds(cx, cy)) {
                continue;
            }
            const PointPx c = centre_of(Cell{cx, cy});
            const long long dx = c.x - centre.x;
            const long long dy = c.y - centre.y;
            if (dx * dx + dy * dy > reach2) {
                continue;
            }
            if (can_see(map, centre, c)) {
                SDL_SetRenderDrawColor(renderer_, 250, 220, 120, 40);
            } else {
                SDL_SetRenderDrawColor(renderer_, 10, 8, 20, 120);
            }
            const SDL_FRect cell = xf.rect(RectPx{cx * kCellPx, cy * kCellPx, kCellPx, kCellPx});
            SDL_RenderFillRect(renderer_, &cell);
        }
    }
}

void MapView::draw_tower_info(const DrawXf& xf, const Map& map, const Sim& sim, int slot,
                              bool selected) const {
    if (slot < 0 || static_cast<std::size_t>(slot) >= sim.blocks().size()) {
        return;
    }
    const Block& b = sim.blocks()[static_cast<std::size_t>(slot)];
    if (!b.alive || !b.armed) {
        return;
    }
    const TowerParams& tp = sim.tower_params(b.kind);
    draw_reach(xf, map, centre_of(b.top_left, b.size_sub()), tp.range_px);
    const RectPx foot = rect_of(b.top_left, b.size_sub());
    const int h = block_height(b.kind);
    if (selected) {
        SDL_SetRenderDrawColor(renderer_, 140, 255, 230, 255);
        const SDL_FRect m = xf.rect(RectPx{foot.x - 2, foot.y - h - 2, foot.w + 4, foot.h + h + 4});
        SDL_RenderRect(renderer_, &m);
    }
    // Its readout over it.
    char label[96];
    std::snprintf(label, sizeof label, "dps %d  dmg %lld  kills %d  %s", sim.tower_dps(slot),
                  static_cast<long long>(b.gun.damage), b.gun.kills,
                  targeting_name(b.gun.targeting));
    const float lx = xf.x(static_cast<float>(foot.x));
    const float ly = xf.y(static_cast<float>(foot.y - h - 12));
    const SDL_FRect back{lx - 1.0f, ly - 1.0f, static_cast<float>(std::strlen(label)) * 8.0f + 2.0f,
                         10.0f};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 160);
    SDL_RenderFillRect(renderer_, &back);
    SDL_SetRenderDrawColor(renderer_, 170, 255, 235, 255);
    SDL_RenderDebugText(renderer_, lx, ly, label);
}

void MapView::draw_probe_preview(const DrawXf& xf, const Map& map, SubCell top_left, bool legal,
                                 int range_px) const {
    draw_reach(xf, map, centre_of(top_left, kLightSub), range_px);
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
