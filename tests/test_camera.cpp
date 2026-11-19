#include "core/camera.hpp"

#include <initializer_list>

#include <doctest/doctest.h>

#include "core/grid.hpp"

using namespace pixeltd;

namespace {

// True if the window's steps are exactly `expected` (closest first) with the standard at index 1.
bool steps_are(int w, int h, std::initializer_list<Zoom> expected) {
    const ZoomSteps s = zoom_steps(w, h);
    if (s.count != static_cast<int>(expected.size()) || s.standard != 1) {
        return false;
    }
    int i = 0;
    for (const Zoom z : expected) {
        if (!(s.steps[static_cast<std::size_t>(i)] == z)) {
            return false;
        }
        ++i;
    }
    return true;
}

} // namespace

TEST_CASE("the view fills the window, rounded up") {
    CHECK(view_size(1920, 1080, {3, 1}) == ViewSize{640, 360});
    CHECK(view_size(1366, 768, {2, 1}) == ViewSize{683, 384});
    CHECK(view_size(1366, 768, {3, 1}) == ViewSize{456, 256});
    CHECK(view_size(100, 100, {0, 0}) ==
          ViewSize{100, 100}); // a zoom < 1/1 in either part acts as 1
}

TEST_CASE("fractional zoom shows more of the map") {
    CHECK(view_size(1920, 1080, {1, 2}) == ViewSize{3840, 2160});
    CHECK(view_size(1920, 1080, {1920, 5120}) == ViewSize{5120, 2880}); // the whole map, exactly
    // 1366 x 768 is a little wider than 16:9, so the height decides and the width overshoots.
    CHECK(whole_map_zoom(1366, 768) == Zoom{768, 2880});
    CHECK(view_size(1366, 768, {768, 2880}) == ViewSize{5123, 2880});
    CHECK(whole_map_zoom(1920, 1080) == Zoom{1920, 5120});
    CHECK(whole_map_zoom(1000, 1000) == Zoom{1000, 5120}); // square: the width decides
}

TEST_CASE("zoom steps for common windows") {
    // The map is 5120 x 2880: every common window zooms out past 1x to the whole map.
    CHECK(steps_are(1920, 1080, {{4, 1}, {3, 1}, {2, 1}, {1, 1}, {1, 2}, {1920, 5120}}));
    CHECK(steps_are(1366, 768, {{3, 1}, {2, 1}, {1, 1}, {1, 2}, {768, 2880}}));
    // 1/2 already shows the whole map on 1440p, so there's no separate 1/2 step.
    CHECK(steps_are(2560, 1440, {{5, 1}, {4, 1}, {3, 1}, {2, 1}, {1, 1}, {2560, 5120}}));
    CHECK(steps_are(3840, 2160,
                    {{7, 1}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {2, 1}, {1, 1}, {3840, 5120}}));
    CHECK(steps_are(1280, 720, {{3, 1}, {2, 1}, {1, 1}, {1, 2}, {1280, 5120}}));
}

TEST_CASE("windows bigger than the map, small and odd windows") {
    // 6144 x 3456: 1x would show more than the map, so whole steps stop at 2x; the whole map is
    // then shown at 1.2.
    CHECK(steps_are(
        6144, 3456,
        {{10, 1}, {9, 1}, {8, 1}, {7, 1}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {2, 1}, {6144, 5120}}));
    CHECK(steps_are(600, 300, {{2, 1}, {1, 1}, {1, 2}, {300, 2880}})); // smaller than 640x360
    // 8000 x 800: 1x would be wider than the map, so the standard steps in to 2x.
    CHECK(steps_are(8000, 800, {{3, 1}, {2, 1}, {800, 2880}}));
    CHECK(steps_are(0, 0, {{2, 1}, {1, 1}, {1, 2}, {1, 5120}}));
}

TEST_CASE("the HUD's relative resolution: the map area shown and its share") {
    CHECK(visible_area({640, 360}) == ViewSize{640, 360});
    CHECK(map_share_permille({640, 360}) == 15);    // 1/64 of the map: 1.5%
    CHECK(map_share_permille({3840, 2160}) == 562); // the 1/2 step on 1080p: 56.2%
    CHECK(map_share_permille({5120, 2880}) == 1000);
    // The whole-map step on 1366 x 768 is 3 px wider than the map: clipped to it.
    CHECK(visible_area({5123, 2880}) == ViewSize{5120, 2880});
    CHECK(map_share_permille({5123, 2880}) == 1000);
}

TEST_CASE("only whole-number steps are pixel-perfect") {
    CHECK(pixel_perfect({3, 1}));
    CHECK_FALSE(pixel_perfect({1, 2}));
    CHECK_FALSE(pixel_perfect({1920, 5120}));
    CHECK(farther({1, 2}, {1, 1}));
    CHECK_FALSE(farther({2560, 5120}, {1, 2})); // equal is not farther
}

TEST_CASE("the camera stays inside the map") {
    const ViewSize v{640, 360};
    CHECK(clamp_camera({-50, -10}, v) == CameraPos{0, 0});
    CHECK(clamp_camera({9000, 9000}, v) == CameraPos{kMapPxW - 640, kMapPxH - 360});
    CHECK(clamp_camera({100, 200}, v) == CameraPos{100, 200});
    // A view as tall as the map can't move vertically.
    CHECK(clamp_camera({300, 50}, ViewSize{1280, kMapPxH}) == CameraPos{300, 0});
    // Wider than the map: centred.
    CHECK(clamp_camera({300, 50}, ViewSize{5200, 400}) == CameraPos{-40, 50});
    // The whole-map step: centred (with a pixel of bar on one side here).
    CHECK(clamp_camera({700, 300}, ViewSize{5123, 2880}) == CameraPos{-1, 0});
}

TEST_CASE("zooming keeps the world point under the cursor") {
    const CameraPos cam{300, 120};
    const int mx = 960;
    const int my = 540;
    const CameraPos in = zoom_about(cam, {3, 1}, {4, 1}, mx, my);
    CHECK(in.x + mx / 4 == cam.x + mx / 3);
    CHECK(in.y + my / 4 == cam.y + my / 3);
    const CameraPos out = zoom_about(cam, {3, 1}, {2, 1}, mx, my);
    CHECK(out.x + mx / 2 == cam.x + mx / 3);
    CHECK(out.y + my / 2 == cam.y + my / 3);
    // Past 1x: at 1/2 each window pixel is two world pixels.
    const CameraPos half = zoom_about(cam, {1, 1}, {1, 2}, mx, my);
    CHECK(half.x + mx * 2 == cam.x + mx);
    CHECK(half.y + my * 2 == cam.y + my);
    // To the whole map and back.
    const CameraPos whole = zoom_about(half, {1, 2}, {1920, 5120}, mx, my);
    CHECK(whole.x + mx * 5120 / 1920 == half.x + mx * 2);
    const CameraPos back = zoom_about(whole, {1920, 5120}, {1, 2}, mx, my);
    CHECK(back.x + mx * 2 == whole.x + mx * 5120 / 1920);
}
