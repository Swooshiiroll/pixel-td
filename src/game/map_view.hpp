#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "core/camera.hpp"
#include "core/grid.hpp"
#include "core/map.hpp"

namespace pixeltd::game {

// Draws a generated map. The terrain is baked once per map into a 3840x2160 texture, seamless:
// height is interpolated between cell centres, brightness shows it, hill shading shows slopes and
// hue shows the material. The core and spawns are drawn over it each frame. Colours are
// placeholders until the style board settles the art.
class MapView {
  public:
    explicit MapView(SDL_Renderer* renderer);
    ~MapView();
    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;

    // Bakes the terrain of `map`. False if the texture couldn't be created or updated.
    bool set_map(const Map& map);

    // Draws into the current render target, whose top-left shows world pixel `cam`: the terrain,
    // then the core and the spawn markers.
    void draw(CameraPos cam, ViewSize view) const;

    // How long the last bake took, in milliseconds.
    [[nodiscard]] double bake_ms() const noexcept { return bake_ms_; }

  private:
    SDL_Renderer* renderer_;
    SDL_Texture* terrain_ = nullptr;
    std::vector<std::uint8_t> pixels_; // RGBA, reused for every map
    std::optional<Cell> core_;
    std::vector<Spawn> spawns_;
    double bake_ms_ = 0.0;
};

} // namespace pixeltd::game
