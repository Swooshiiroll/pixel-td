#include "core/camera.hpp"

#include <cstddef>
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
    CHECK(view_size(1920, 1080, {1, 4}) == ViewSize{7680, 4320});
    CHECK(view_size(1920, 1080, {1920, 15360}) == ViewSize{15360, 8640}); // the whole map, exactly
    // 1366 x 768 is a little wider than 16:9, so the height decides and the width overshoots.
    CHECK(whole_map_zoom(1366, 768) == Zoom{768, 8640});
    CHECK(view_size(1366, 768, {768, 8640}) == ViewSize{15368, 8640});
    CHECK(whole_map_zoom(1920, 1080) == Zoom{1920, 15360});
    CHECK(whole_map_zoom(1000, 1000) == Zoom{1000, 15360}); // square: the width decides
}

TEST_CASE("zoom steps for common windows") {
    // The map is 15360 x 8640: past 1x the steps halve until the whole map fits.
    CHECK(steps_are(1920, 1080,
                    {{4, 1}, {3, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 4}, {1920, 15360}})); // = 1/8
    CHECK(steps_are(1366, 768, {{3, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 4}, {1, 8}, {768, 8640}}));
    CHECK(steps_are(2560, 1440,
                    {{5, 1}, {4, 1}, {3, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 4}, {2560, 15360}}));
    // 4K: the whole map is exactly 1/4, so there's no separate 1/4 step.
    CHECK(
        steps_are(3840, 2160,
                  {{7, 1}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {2, 1}, {1, 1}, {1, 2}, {3840, 15360}}));
    CHECK(steps_are(1280, 720, {{3, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 4}, {1, 8}, {1280, 15360}}));
}

TEST_CASE("windows bigger than the map, small and odd windows") {
    // 16384 x 9216: 1x would show more than the map, so whole steps stop at 2x; then the whole map
    // at about 1.07.
    const ZoomSteps big = zoom_steps(16384, 9216);
    CHECK(big.steps[1] == Zoom{25, 1}); // the standard: 16384 / 25 x 9216 / 25 = 656 x 369
    CHECK(big.count == 26);             // 26x down to 2x, then the whole map
    CHECK(big.steps[24] == Zoom{2, 1});
    CHECK(big.steps[25] == Zoom{16384, 15360});
    // Smaller than 640 x 360.
    CHECK(steps_are(600, 300, {{2, 1}, {1, 1}, {1, 2}, {1, 4}, {1, 8}, {1, 16}, {300, 8640}}));
    CHECK(steps_are(8000, 800, {{3, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 4}, {1, 8}, {800, 8640}}));
    // A 1 x 1 window: halving stops at 1/1024, then the whole map.
    const ZoomSteps tiny = zoom_steps(0, 0);
    CHECK(tiny.count == 13);
    CHECK(tiny.steps[11] == Zoom{1, 1024});
    CHECK(tiny.steps[12] == Zoom{1, 15360});
}

TEST_CASE("the HUD's relative resolution: the map area shown and its share") {
    CHECK(visible_area({640, 360}) == ViewSize{640, 360});
    CHECK(map_share_bp({640, 360}) == 17);    // 0.17% of the map
    CHECK(map_share_bp({3840, 2160}) == 625); // the 1/2 step on 1080p: 6.25%
    CHECK(map_share_bp({15360, 8640}) == 10000);
    // The whole-map step on 1366 x 768 is 8 px wider than the map: clipped to it.
    CHECK(visible_area({15368, 8640}) == ViewSize{15360, 8640});
    CHECK(map_share_bp({15368, 8640}) == 10000);
}

TEST_CASE("only whole-number steps are pixel-perfect") {
    CHECK(pixel_perfect({3, 1}));
    CHECK_FALSE(pixel_perfect({1, 2}));
    CHECK_FALSE(pixel_perfect({1920, 15360}));
    CHECK(farther({1, 2}, {1, 1}));
    CHECK_FALSE(farther({2560, 5120}, {1, 2})); // equal is not farther
}

TEST_CASE("the camera stays inside the map") {
    const ViewSize v{640, 360};
    CHECK(clamp_camera({-50, -10}, v) == CameraPos{0, 0});
    CHECK(clamp_camera({99999, 99999}, v) == CameraPos{kMapPxW - 640, kMapPxH - 360});
    CHECK(clamp_camera({100, 200}, v) == CameraPos{100, 200});
    // A view as tall as the map can't move vertically.
    CHECK(clamp_camera({300, 50}, ViewSize{1280, kMapPxH}) == CameraPos{300, 0});
    // Wider than the map: centred.
    CHECK(clamp_camera({300, 50}, ViewSize{16000, 400}) == CameraPos{-320, 50});
    // The whole-map step: centred (with a few pixels of bar on each side here).
    CHECK(clamp_camera({700, 300}, ViewSize{15368, 8640}) == CameraPos{-4, 0});
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
    const CameraPos whole = zoom_about(half, {1, 2}, {1920, 15360}, mx, my);
    CHECK(whole.x + mx * 15360 / 1920 == half.x + mx * 2);
    const CameraPos back = zoom_about(whole, {1920, 15360}, {1, 2}, mx, my);
    CHECK(back.x + mx * 2 == whole.x + mx * 15360 / 1920);
}
