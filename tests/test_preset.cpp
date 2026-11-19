#include "core/preset.hpp"

#include <string>

#include <doctest/doctest.h>

using namespace pixeltd;

TEST_CASE("a preset round-trips every value exactly") {
    GenParams p;
    p.height = NoiseSpec{80, 5, 60, 45, Curve::Quintic, Shape::Ridged, 10, 30};
    p.mesa_noise.shape = Shape::Billowed;
    p.material_noise.curve = Curve::Linear;
    p.formation_rough.warp_cells = 4;
    p.mesa_feather_cells = 12;
    p.mesa_relief = 3;
    p.core_area = Rect{90, 50, 140, 80};
    p.spawn_waves = {1, 5, 15};
    p.range_pct_per_10 = 7;
    REQUIRE(validate(p) == 0);

    const std::string text = to_preset(p);
    GenParams q; // defaults, overwritten by the preset
    std::string errors;
    CHECK(from_preset(text, q, errors));
    CHECK(errors.empty());
    CHECK(q == p);
    CHECK(to_preset(q) == text);
}

TEST_CASE("a preset holds settings only, never a seed") {
    const std::string text = to_preset(GenParams{});
    CHECK(text.find("seed =") == std::string::npos);
    CHECK(text.find("height.curve = smoothstep") != std::string::npos);
    CHECK(text.find("spawn_waves = 1, 10, 20") != std::string::npos);
}

TEST_CASE("missing keys keep their values; comments and blank lines are ignored") {
    GenParams p;
    p.mesa_band = 30;
    std::string errors;
    CHECK(from_preset("# just one change\n\n  mesa_relief = 4   # trailing comment\n", p, errors));
    CHECK(p.mesa_relief == 4);
    CHECK(p.mesa_band == 30);
    CHECK(p.height == GenParams{}.height);
}

TEST_CASE("bad presets are reported, and values are clamped") {
    GenParams p;
    std::string errors;
    CHECK_FALSE(from_preset("no_such_key = 3\n"
                            "height.octaves = many\n"
                            "height.curve = wobbly\n"
                            "just text\n"
                            "spawn_waves = 1, 2\n"
                            "height.step = 9999\n",
                            p, errors));
    CHECK(errors.find("line 1: unknown key 'no_such_key'") != std::string::npos);
    CHECK(errors.find("line 2: bad value for 'height.octaves'") != std::string::npos);
    CHECK(errors.find("line 3: bad value for 'height.curve'") != std::string::npos);
    CHECK(errors.find("line 4: expected key = value") != std::string::npos);
    CHECK(errors.find("line 5: bad value for 'spawn_waves'") != std::string::npos);
    CHECK(errors.find("clamped") != std::string::npos);
    CHECK(p.height.step == kMapW / 2);                     // clamped
    CHECK(p.height.octaves == GenParams{}.height.octaves); // unchanged after a bad value
    CHECK(p.spawn_waves == GenParams{}.spawn_waves);
}

TEST_CASE("enum names") {
    CHECK(std::string(curve_name(Curve::Quintic)) == "quintic");
    CHECK(std::string(shape_name(Shape::Billowed)) == "billowed");
}
