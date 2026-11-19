#include "core/camera.hpp"

#include <doctest/doctest.h>

#include "core/grid.hpp"

using namespace pixeltd;

TEST_CASE("the view fills the window, rounded up") {
    CHECK(view_size(1920, 1080, 3) == ViewSize{640, 360});
    CHECK(view_size(1366, 768, 2) == ViewSize{683, 384});
    CHECK(view_size(1366, 768, 3) == ViewSize{456, 256});
    CHECK(view_size(100, 100, 0) == ViewSize{100, 100}); // scale < 1 acts as 1
}

TEST_CASE("zoom steps for common windows") {
    // {closest, standard, farthest}
    // The map is 3840 x 2160, so every common window can zoom out to 1x.
    CHECK(zoom_limits(1920, 1080) == ZoomLimits{4, 3, 1});
    CHECK(zoom_limits(1366, 768) == ZoomLimits{3, 2, 1});
    CHECK(zoom_limits(2560, 1440) == ZoomLimits{5, 4, 1});
    CHECK(zoom_limits(1280, 720) == ZoomLimits{3, 2, 1});
    CHECK(zoom_limits(3840, 2160) == ZoomLimits{7, 6, 1}); // 1x shows exactly the whole map
}

TEST_CASE("small and odd windows") {
    CHECK(zoom_limits(600, 300) == ZoomLimits{2, 1, 1}); // smaller than 640x360: 1x, what fits
    // 8000 x 800: 2x would show 4000 px, wider than the 3840 px map, so the standard steps in.
    CHECK(zoom_limits(8000, 800) == ZoomLimits{4, 3, 3});
    CHECK(zoom_limits(4000, 800) == ZoomLimits{3, 2, 2}); // 1x (4000 px) is wider than the map
    CHECK(zoom_limits(0, 0) == ZoomLimits{2, 1, 1});
}

TEST_CASE("the camera stays inside the map") {
    const ViewSize v{640, 360};
    CHECK(clamp_camera({-50, -10}, v) == CameraPos{0, 0});
    CHECK(clamp_camera({5000, 5000}, v) == CameraPos{kMapPxW - 640, kMapPxH - 360});
    CHECK(clamp_camera({100, 200}, v) == CameraPos{100, 200});
    // A view as tall as the map can't move vertically.
    CHECK(clamp_camera({300, 50}, ViewSize{1280, kMapPxH}) == CameraPos{300, 0});
    // Wider than the map: centred.
    CHECK(clamp_camera({300, 50}, ViewSize{4000, 400}) == CameraPos{-80, 50});
}

TEST_CASE("zooming keeps the world point under the cursor") {
    const CameraPos cam{300, 120};
    const int mx = 960;
    const int my = 540;
    const CameraPos in = zoom_about(cam, 3, 4, mx, my);
    CHECK(in.x + mx / 4 == cam.x + mx / 3);
    CHECK(in.y + my / 4 == cam.y + my / 3);
    const CameraPos out = zoom_about(cam, 3, 2, mx, my);
    CHECK(out.x + mx / 2 == cam.x + mx / 3);
    CHECK(out.y + my / 2 == cam.y + my / 3);
}
