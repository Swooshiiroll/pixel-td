#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "core/camera.hpp"
#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd::game {

// Draws a generated map. The terrain is baked once per map into a 1920x720 texture; overlays (the
// core zone; later the path and fog) are drawn over it each frame. Colours are placeholders until
// the style board settles the art.
class MapView {
  public:
    explicit MapView(SDL_Renderer* renderer);
    ~MapView();
    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;

    // Bakes the terrain of `map`. False if the texture couldn't be created or updated.
    bool set_map(const Map& map);

    // Draws into the current render target, whose top-left shows world pixel `cam`: the terrain
    // and path, then the zone (while placing) or the core and the spawn marker.
    void draw(CameraPos cam, ViewSize view) const;

    // The 6x6 placement ghost with its top-left at `top_left`: green if legal, red if not.
    void draw_ghost(CameraPos cam, Cell top_left, bool legal) const;

  private:
    SDL_Renderer* renderer_;
    SDL_Texture* terrain_ = nullptr;
    std::vector<std::uint8_t> pixels_; // RGBA, reused for every map
    Rect zone_{};
    std::optional<Cell> core_;
    Cell spawn_{};
};

} // namespace pixeltd::game
