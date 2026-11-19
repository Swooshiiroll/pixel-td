#include "core/camera.hpp"

#include <algorithm>
#include <cstddef>

#include "core/grid.hpp"

namespace pixeltd {
namespace {

constexpr bool fits_map(ViewSize v) noexcept {
    return v.w <= kMapPxW && v.h <= kMapPxH;
}

constexpr bool at_least_standard(ViewSize v) noexcept {
    return v.w >= kStandardViewW && v.h >= kStandardViewH;
}

constexpr int ceil_mul_div(int value, int mul, int div) noexcept {
    const long long v = static_cast<long long>(value) * mul;
    return static_cast<int>((v + div - 1) / div);
}

} // namespace

ViewSize view_size(int window_w, int window_h, Zoom zoom) noexcept {
    const int num = std::max(zoom.num, 1);
    const int den = std::max(zoom.den, 1);
    return {ceil_mul_div(std::max(window_w, 1), den, num),
            ceil_mul_div(std::max(window_h, 1), den, num)};
}

Zoom whole_map_zoom(int window_w, int window_h) noexcept {
    const int w = std::max(window_w, 1);
    const int h = std::max(window_h, 1);
    // The axis with less room per map pixel decides.
    if (static_cast<long long>(w) * kMapPxH <= static_cast<long long>(h) * kMapPxW) {
        return {w, kMapPxW};
    }
    return {h, kMapPxH};
}

ZoomSteps zoom_steps(int window_w, int window_h) noexcept {
    const auto whole = [&](int k) { return view_size(window_w, window_h, Zoom{k, 1}); };
    // Standard: the largest scale that still shows at least 640 x 360 (1x for small windows).
    int standard = 1;
    while (at_least_standard(whole(standard + 1))) {
        ++standard;
    }
    // Very wide or tall windows: step in until the standard view fits inside the map.
    while (!fits_map(whole(standard))) {
        ++standard;
    }
    int farthest = standard;
    while (farthest > 1 && fits_map(whole(farthest - 1))) {
        --farthest;
    }

    ZoomSteps out;
    const auto push = [&](Zoom z) {
        if (out.count < static_cast<int>(out.steps.size())) {
            out.steps[static_cast<std::size_t>(out.count)] = z;
            ++out.count;
        }
    };
    for (int k = standard + 1; k >= farthest; --k) {
        push({k, 1});
    }
    out.standard = 1;
    const Zoom fit = whole_map_zoom(window_w, window_h);
    if (farthest == 1 && farther(fit, Zoom{1, 2})) {
        push({1, 2});
    }
    if (farther(fit, out.steps[static_cast<std::size_t>(out.count - 1)])) {
        push(fit);
    }
    return out;
}

ViewSize visible_area(ViewSize view) noexcept {
    return {std::clamp(view.w, 0, kMapPxW), std::clamp(view.h, 0, kMapPxH)};
}

int map_share_permille(ViewSize view) noexcept {
    const ViewSize a = visible_area(view);
    const long long shown = static_cast<long long>(a.w) * a.h;
    const long long map = static_cast<long long>(kMapPxW) * kMapPxH;
    return static_cast<int>(shown * 1000 / map);
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

CameraPos zoom_about(CameraPos cam, Zoom from, Zoom to, int mouse_x, int mouse_y) noexcept {
    const auto px = [](int mouse, Zoom z) {
        return static_cast<int>(static_cast<long long>(mouse) * std::max(z.den, 1) /
                                std::max(z.num, 1));
    };
    const int world_x = cam.x + px(mouse_x, from);
    const int world_y = cam.y + px(mouse_y, from);
    return {world_x - px(mouse_x, to), world_y - px(mouse_y, to)};
}

} // namespace pixeltd
