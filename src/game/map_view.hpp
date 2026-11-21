#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/camera.hpp"
#include "core/fog.hpp"
#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/los.hpp"
#include "core/map.hpp"
#include "core/mapgen.hpp"
#include "core/overlay.hpp"
#include "core/sim.hpp"

namespace pixeltd::game {

// World to screen: screen = (world - cam) * k. At whole-number zoom steps the world is drawn 1:1
// (k = 1) into a view target that's then shown at the scale, crisp. At fractional steps (zoomed
// out past 1x) it's drawn straight into a window-sized target at k = the zoom, smooth.
struct DrawXf {
    CameraPos cam{};
    float k = 1.0f;
    bool smooth = false; // filter the terrain (fractional zoom) instead of nearest pixels

    [[nodiscard]] float x(float wx) const noexcept { return (wx - static_cast<float>(cam.x)) * k; }
    [[nodiscard]] float y(float wy) const noexcept { return (wy - static_cast<float>(cam.y)) * k; }
    [[nodiscard]] SDL_FRect rect(const RectPx& r) const noexcept {
        return {x(static_cast<float>(r.x)), y(static_cast<float>(r.y)), static_cast<float>(r.w) * k,
                static_cast<float>(r.h) * k};
    }
};

// Draws a generated map (issue #25) in a 3/4 view (#35). The terrain is one texture with a texel
// per cell (960 x 540, about 2 MB at any map size): each cell gets a colour (its ground, a
// mountain's roof, or the cliff wall that covers the open cell south of rock), and the GPU
// stretches it with linear filtering, so each pixel blends the four nearest cell centres: seamless,
// with nothing baked at full size. Over it, at 1/2 zoom and closer, small generated sprites: the
// cliff walls, standing trees and bank lips; the core and spawns stand as boxes at any zoom. The
// fog is a second cell texture. Looks only: every rule stays on the flat ground grid. Colours are
// placeholders until the style board settles the art.
class MapView {
  public:
    explicit MapView(SDL_Renderer* renderer);
    ~MapView();
    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;

    // Colours the map's cells in the current overlay and uploads them, and the fog layer. A few
    // milliseconds. False if a texture couldn't be created or updated.
    bool set_map(const Map& map, const GenLayers& layers, const Fog& fog);

    // Switches the developer overlay (issue #13) and recolours the cells: Off shows the terrain,
    // the others a noise layer in greyscale.
    bool set_overlay(Overlay overlay, const Map& map, const GenLayers& layers, const Fog& fog);
    [[nodiscard]] Overlay overlay() const noexcept { return overlay_; }

    // Refreshes the fog layer for the cells in `cells` (after fog lifts there).
    bool update_fog(const Fog& fog, const Rect& cells);

    // Developer toggle (F): with the fog hidden, there's no haze and every spawn shows. Exploring
    // carries on underneath.
    void set_fog_shown(bool shown) noexcept { fog_shown_ = shown; }
    [[nodiscard]] bool fog_shown() const noexcept { return fog_shown_; }

    // Draws into the current render target: the terrain, then what stands on it (cliff walls,
    // trees, blocks, walkers, the core, the spawns that have been found), then the fog. Walkers on
    // unexplored ground are hidden while the fog is shown. `view` is the world area the target
    // shows, in world pixels.
    void draw(const DrawXf& xf, ViewSize view, const Fog& fog, const Sim& sim) const;

    // Developer view (R, #11): each found spawn's route to the core, labelled with its length and
    // walk time; a sealed spawn's route follows the breach route, and the block its walkers will
    // attack is outlined.
    void draw_routes(const DrawXf& xf, const Fog& fog, const Sim& sim) const;

    // Build mode's preview: a block of `kind` at `top_left`, green if it can go there, red if not.
    void draw_block_preview(const DrawXf& xf, SubCell top_left, BlockKind kind, bool legal) const;

    // Developer probe preview: a light-tower-sized probe at `top_left` (yellow if it can go there,
    // red if not), with the cells in its range it can see tinted and those hidden behind rock
    // formations shaded.
    void draw_probe_preview(const DrawXf& xf, const Map& map, SubCell top_left, bool legal,
                            int range_px) const;

    // A placed probe.
    void draw_probe(const DrawXf& xf, SubCell top_left, SDL_Color colour) const;

    // How long the last colouring and upload took, in milliseconds.
    [[nodiscard]] double bake_ms() const noexcept { return bake_ms_; }

  private:
    bool create_textures();
    bool create_sprites();
    // The 3/4 view (#35): what lies flat on the ground (bank lips, tree shadows), then what stands
    // (cliff walls, trees, the core, spawns) in rows from north to south, for the cells in `span`.
    void draw_ground(const DrawXf& xf, const Rect& span) const;
    void draw_standing(const DrawXf& xf, const Rect& span, bool sprites, const Fog& fog,
                       const Sim& sim) const;
    void draw_core(const DrawXf& xf) const;
    void draw_spawn(const DrawXf& xf, const Spawn& s) const;
    void draw_block(const DrawXf& xf, const Block& b) const;
    void draw_walker(const DrawXf& xf, const Walker& w, int max_health) const;
    // A health bar `w` px wide with its top-left at (x, y), in world pixels.
    void draw_health(const DrawXf& xf, int x, int y, int w, int health, int max_health) const;
    // A spawn's top face: its 2 x 2 cells, raised by its height.
    [[nodiscard]] static RectPx spawn_top(const Spawn& s);
    void fill_cells(const Map& map, const GenLayers& layers);
    bool upload_terrain();

    SDL_Renderer* renderer_;
    SDL_Texture* terrain_ = nullptr;  // one texel per cell
    SDL_Texture* fog_ = nullptr;      // one texel per cell
    SDL_Texture* sprites_ = nullptr;  // the sprite atlas: trees, their shadow, walls, the bank lip
    std::vector<std::uint8_t> marks_; // per cell: has a tree, a cliff wall, a bank lip
    Overlay overlay_ = Overlay::Off;
    bool fog_shown_ = true;
    std::vector<std::array<float, 3>> cell_; // each cell's colour (terrain or overlay)
    std::vector<std::uint8_t> terrain_px_;   // ...as RGBA, for the upload
    std::vector<std::uint8_t> fog_px_;       // RGBA, one texel per cell
    std::optional<SubCell> core_;
    std::vector<Spawn> spawns_;
    double bake_ms_ = 0.0;

    // Blocks and walkers in view, sorted into their foot rows for the standing pass. Reused every
    // frame, so drawing allocates nothing once it has grown.
    struct Standing {
        int row = 0;
        int kind = 0; // 0 a block, 1 a walker (blocks first in a row)
        int index = 0;
    };
    mutable std::vector<Standing> standing_;

    // The routes the developer view shows, recomputed only when the sim rebuilds its routes.
    struct ShownRoute {
        Route route;
        float length_px = 0.0f;
    };
    mutable std::vector<ShownRoute> routes_;
    mutable int routes_build_ = -1;
    mutable std::vector<SDL_FPoint> route_points_; // reused for drawing a route's line
};

} // namespace pixeltd::game
