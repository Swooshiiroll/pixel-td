#include "core/camera.hpp"

#include <algorithm>

#include "core/grid.hpp"

namespace pixeltd {
namespace {

constexpr int ceil_div(int a, int b) noexcept {
    return (a + b - 1) / b;
}

constexpr bool fits_map(ViewSize v) noexcept {
    return v.w <= kMapPxW && v.h <= kMapPxH;
}

constexpr bool at_least_standard(ViewSize v) noexcept {
    return v.w >= kStandardViewW && v.h >= kStandardViewH;
}

} // namespace

ViewSize view_size(int window_w, int window_h, int scale) noexcept {
    const int s = std::max(scale, 1);
    return {ceil_div(std::max(window_w, 1), s), ceil_div(std::max(window_h, 1), s)};
}

ZoomLimits zoom_limits(int window_w, int window_h) noexcept {
    // Standard: the largest scale that still shows at least 640 x 360 (1x for small windows).
    int standard = 1;
    while (at_least_standard(view_size(window_w, window_h, standard + 1))) {
        ++standard;
    }
    // Very wide or tall windows: step in until the standard view fits inside the map, because
    // nothing outside the map is ever shown.
    while (!fits_map(view_size(window_w, window_h, standard))) {
        ++standard;
    }
    int farthest = standard;
    while (farthest > 1 && fits_map(view_size(window_w, window_h, farthest - 1))) {
        --farthest;
    }
    return {standard + 1, standard, farthest};
}

CameraPos clamp_camera(CameraPos cam, ViewSize view) noexcept {
    const auto axis = [](int pos, int view_len, int map_len) {
        if (view_len >= map_len) {
            return (map_len - view_len) / 2;
        }
        return std::clamp(pos, 0, map_len - view_len);
    };
    return {axis(cam.x, view.w, kMapPxW), axis(cam.y, view.h, kMapPxH)};
}

CameraPos zoom_about(CameraPos cam, int old_scale, int new_scale, int mouse_x,
                     int mouse_y) noexcept {
    const int o = std::max(old_scale, 1);
    const int n = std::max(new_scale, 1);
    const int world_x = cam.x + mouse_x / o;
    const int world_y = cam.y + mouse_y / o;
    return {world_x - mouse_x / n, world_y - mouse_y / n};
}

} // namespace pixeltd
