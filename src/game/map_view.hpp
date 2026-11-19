#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/camera.hpp"
#include "core/contours.hpp"
#include "core/fog.hpp"
#include "core/gen_params.hpp"
#include "core/grid.hpp"
#include "core/los.hpp"
#include "core/map.hpp"
#include "core/mapgen.hpp"
#include "core/overlay.hpp"

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

// Draws a generated map (issue #25). The terrain is one texture with a texel per cell (960 x 540,
// about 2 MB at any map size): each cell gets a colour (brightness from height, hill shading, muted
// material hue, formation shadows), and the GPU stretches it with linear filtering, so each pixel
// blends the four nearest cell centres: seamless, with nothing baked at full size. The fog is a
// second cell texture. The elevation overlay adds contour lines (core/contours). The core, spawns
// and probes are drawn over it each frame. Colours are placeholders until the style board settles
// the art.
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
    // Elevation colour bands with contours, the rest a noise layer in greyscale.
    bool set_overlay(Overlay overlay, const Map& map, const GenLayers& layers, const Fog& fog);
    [[nodiscard]] Overlay overlay() const noexcept { return overlay_; }

    // Refreshes the fog layer for the cells in `cells` (after fog lifts there).
    bool update_fog(const Fog& fog, const Rect& cells);

    // Developer toggle (F): with the fog hidden, there's no haze and every spawn shows. Exploring
    // carries on underneath.
    void set_fog_shown(bool shown) noexcept { fog_shown_ = shown; }
    [[nodiscard]] bool fog_shown() const noexcept { return fog_shown_; }

    // Draws into the current render target: the terrain, the fog, then the core and the spawns
    // that have been found. `view` is the world area the target shows, in world pixels.
    void draw(const DrawXf& xf, ViewSize view, const Fog& fog) const;

    // Developer probe preview: a light-tower-sized probe at `top_left` (yellow if it can go there,
    // red if not), with the cells it would reach and see tinted and the rest of its possible
    // reach shaded.
    void draw_probe_preview(const DrawXf& xf, const Map& map, const GenParams& p, SubCell top_left,
                            bool legal, int base_px) const;

    // A placed probe.
    void draw_probe(const DrawXf& xf, SubCell top_left, SDL_Color colour) const;

    // How long the last colouring and upload took, in milliseconds.
    [[nodiscard]] double bake_ms() const noexcept { return bake_ms_; }

  private:
    bool create_textures();
    void fill_cells(const Map& map, const GenLayers& layers);
    bool upload_terrain();

    SDL_Renderer* renderer_;
    SDL_Texture* terrain_ = nullptr; // one texel per cell
    SDL_Texture* fog_ = nullptr;     // one texel per cell
    Overlay overlay_ = Overlay::Off;
    bool fog_shown_ = true;
    std::vector<std::array<float, 3>> cell_; // each cell's colour (terrain or overlay)
    std::vector<std::uint8_t> terrain_px_;   // ...as RGBA, for the upload
    std::vector<std::uint8_t> fog_px_;       // RGBA, one texel per cell
    Contours contours_;                      // the elevation overlay's lines (else empty)
    std::optional<SubCell> core_;
    std::vector<Spawn> spawns_;
    double bake_ms_ = 0.0;
};

} // namespace pixeltd::game
