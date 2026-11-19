#pragma once

#include <array>

namespace pixeltd {

// Camera and zoom maths (design/world.md, Camera). At whole-number zoom steps the world is drawn
// 1:1 into a view the size of the window divided by the scale, so pixels stay crisp. Zooming out
// continues past 1x to 1/2 and the whole map; those fractional steps are drawn shrunk and smooth.

inline constexpr int kStandardViewW = 640;
inline constexpr int kStandardViewH = 360;

struct ViewSize {
    int w = 0;
    int h = 0;

    friend constexpr bool operator==(ViewSize, ViewSize) = default;
};

// A zoom: num / den screen pixels per world pixel. Whole-number steps are {k, 1}.
struct Zoom {
    int num = 1;
    int den = 1;

    friend constexpr bool operator==(Zoom, Zoom) = default;
};

// True for whole-number steps, which are drawn pixel-perfect.
[[nodiscard]] constexpr bool pixel_perfect(Zoom z) noexcept {
    return z.den == 1;
}

// True if `a` shows world pixels smaller than `b` does (a is farther out).
[[nodiscard]] constexpr bool farther(Zoom a, Zoom b) noexcept {
    return static_cast<long long>(a.num) * b.den < static_cast<long long>(b.num) * a.den;
}

// The zoom steps for a window, closest first:
// - whole-number steps from one closer than the standard down to the last whole step whose view
//   still fits inside the map (1x unless the window is bigger than the map);
// - then 1/2, if the farthest whole step is 1x and the whole map needs less than half;
// - then the whole map, fitted to the window, if it's farther than the last step.
// `standard` is the default step: the largest whole scale whose view is at least 640 x 360 (and
// fits inside the map).
struct ZoomSteps {
    std::array<Zoom, 16> steps{};
    int count = 0;
    int standard = 0;
};
[[nodiscard]] ZoomSteps zoom_steps(int window_w, int window_h) noexcept;

// The zoom that fits the whole map in the window (the limiting axis decides).
[[nodiscard]] Zoom whole_map_zoom(int window_w, int window_h) noexcept;

// World pixel at the view's top-left corner.
struct CameraPos {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(CameraPos, CameraPos) = default;
};

// The world pixels the window shows at a zoom: window pixels * den / num, rounded up (the last
// partial pixel is cropped).
[[nodiscard]] ViewSize view_size(int window_w, int window_h, Zoom zoom) noexcept;

// The part of the map a view shows, in world pixels: the view clipped to the map (the whole-map
// step can be wider or taller than the map, with bars).
[[nodiscard]] ViewSize visible_area(ViewSize view) noexcept;

// That area's share of the map, in tenths of a percent (0..1000).
[[nodiscard]] int map_share_permille(ViewSize view) noexcept;

// Keeps the view inside the map; centres it on any axis where the view is wider than the map.
[[nodiscard]] CameraPos clamp_camera(CameraPos cam, ViewSize view) noexcept;

// The camera after zooming from `from` to `to`, keeping the world pixel under the cursor (window
// pixels mouse_x, mouse_y) where it is. Clamp the result afterwards.
[[nodiscard]] CameraPos zoom_about(CameraPos cam, Zoom from, Zoom to, int mouse_x,
                                   int mouse_y) noexcept;

} // namespace pixeltd
