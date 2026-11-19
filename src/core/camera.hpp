#pragma once

namespace pixeltd {

// Camera and zoom maths (design/world.md, Camera). The world is drawn 1:1 into a view the size of
// the window divided by a whole-number scale, so pixels stay crisp at every zoom step.

inline constexpr int kStandardViewW = 640;
inline constexpr int kStandardViewH = 360;

struct ViewSize {
    int w = 0;
    int h = 0;

    friend constexpr bool operator==(ViewSize, ViewSize) = default;
};

// Zoom steps as whole-number scales; a bigger scale is closer.
struct ZoomLimits {
    int closest = 1;  // one step in past the standard view
    int standard = 1; // the default: the largest scale whose view is at least 640 x 360
    int farthest = 1; // the last step out whose view still fits inside the map

    friend constexpr bool operator==(ZoomLimits, ZoomLimits) = default;
};

// World pixel at the view's top-left corner.
struct CameraPos {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(CameraPos, CameraPos) = default;
};

// The view fills the window: window pixels / scale, rounded up (the last partial pixel is cropped).
[[nodiscard]] ViewSize view_size(int window_w, int window_h, int scale) noexcept;

[[nodiscard]] ZoomLimits zoom_limits(int window_w, int window_h) noexcept;

// Keeps the view inside the map; centres it on any axis where the view is wider than the map.
[[nodiscard]] CameraPos clamp_camera(CameraPos cam, ViewSize view) noexcept;

// The camera after zooming from old_scale to new_scale, keeping the world pixel under the cursor
// (window pixels mouse_x, mouse_y) where it is. Clamp the result afterwards.
[[nodiscard]] CameraPos zoom_about(CameraPos cam, int old_scale, int new_scale, int mouse_x,
                                   int mouse_y) noexcept;

} // namespace pixeltd
