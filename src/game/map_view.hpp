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
#include "core/map.hpp"
#include "core/mapgen.hpp"
#include "core/overlay.hpp"

namespace pixeltd::game {

// Draws a generated map. The terrain is baked into a 3840x2160 texture, seamless: each cell gets a
// colour (brightness from height, hill shading, muted material hue, formation shadows, haze if
// unexplored) and each pixel blends the four nearest cell centres. When fog lifts, only that area
// is re-baked. The core, spawns and probes are drawn over it each frame. Colours are placeholders
// until the style board settles the art.
class MapView {
  public:
    explicit MapView(SDL_Renderer* renderer);
    ~MapView();
    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;

    // Bakes the whole map in the current overlay. False if the texture couldn't be created or
    // updated.
    bool set_map(const Map& map, const GenLayers& layers, const Fog& fog);

    // Switches the developer overlay (issue #13) and re-bakes: Off shows the terrain, Elevation
    // colour bands with contours, the rest a noise layer in greyscale.
    bool set_overlay(Overlay overlay, const Map& map, const GenLayers& layers, const Fog& fog);
    [[nodiscard]] Overlay overlay() const noexcept { return overlay_; }

    // Re-bakes the cells in `cells` (after fog lifts there).
    bool update(const Map& map, const Fog& fog, const Rect& cells);

    // Draws into the current render target, whose top-left shows world pixel `cam`: the terrain,
    // then the core and the spawns that have been found.
    void draw(CameraPos cam, ViewSize view, const Fog& fog) const;

    // Developer probe preview: a 2x2 probe at `top_left` (yellow if it can go there, red if not),
    // with the cells it would reach and see tinted and the rest of its possible reach shaded.
    void draw_probe_preview(CameraPos cam, const Map& map, const GenParams& p, Cell top_left,
                            bool legal, int base_px) const;

    // A placed probe.
    void draw_probe(CameraPos cam, Cell top_left, SDL_Color colour) const;

    // How long the last full bake took, in milliseconds.
    [[nodiscard]] double bake_ms() const noexcept { return bake_ms_; }

  private:
    void fill_cells(const Map& map, const GenLayers& layers);
    bool render(const Map& map, const Fog& fog, const Rect& cells);

    SDL_Renderer* renderer_;
    SDL_Texture* terrain_ = nullptr;
    Overlay overlay_ = Overlay::Off;
    std::vector<std::uint8_t> pixels_;        // RGBA, reused for every map
    std::vector<std::array<float, 3>> cell_;  // each cell's colour (terrain or noise layer)
    std::vector<std::array<float, 3>> shown_; // ...with the haze applied
    std::vector<float> haze_;                 // 1 where unexplored, else 0
    std::optional<Cell> core_;
    std::vector<Spawn> spawns_;
    double bake_ms_ = 0.0;
};

} // namespace pixeltd::game
