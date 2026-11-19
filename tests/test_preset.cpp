#include "core/preset.hpp"

#include <string>

#include <doctest/doctest.h>

using namespace pixeltd;

TEST_CASE("a preset round-trips every value exactly") {
    GenParams p;
    p.material_noise = NoiseSpec{80, 5, 60, 45, Curve::Quintic, Shape::Ridged, 10, 30};
    p.formation_rough.shape = Shape::Billowed;
    p.formation_rough.warp_cells = 4;
    p.formations_min = 150;
    p.formations_max = 400;
    p.formation_share_max_pct = 22;
    p.core_area = Rect{90, 50, 140, 80};
    p.spawn_waves = {1, 5, 15};
    p.light_range_px = 90;
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
    CHECK(text.find("material_noise.curve = smoothstep") != std::string::npos);
    CHECK(text.find("spawn_waves = 1, 10, 20") != std::string::npos);
}

TEST_CASE("missing keys keep their values; comments and blank lines are ignored") {
    GenParams p;
    p.formations_max = 300;
    std::string errors;
    CHECK(from_preset("# just one change\n\n  formations_min = 40   # trailing comment\n", p,
                      errors));
    CHECK(p.formations_min == 40);
    CHECK(p.formations_max == 300);
    CHECK(p.material_noise == GenParams{}.material_noise);
}

TEST_CASE("bad presets are reported, and values are clamped") {
    GenParams p;
    std::string errors;
    CHECK_FALSE(from_preset("no_such_key = 3\n"
                            "material_noise.octaves = many\n"
                            "material_noise.curve = wobbly\n"
                            "just text\n"
                            "spawn_waves = 1, 2\n"
                            "material_noise.step = 9999\n",
                            p, errors));
    CHECK(errors.find("line 1: unknown key 'no_such_key'") != std::string::npos);
    CHECK(errors.find("line 2: bad value for 'material_noise.octaves'") != std::string::npos);
    CHECK(errors.find("line 3: bad value for 'material_noise.curve'") != std::string::npos);
    CHECK(errors.find("line 4: expected key = value") != std::string::npos);
    CHECK(errors.find("line 5: bad value for 'spawn_waves'") != std::string::npos);
    CHECK(errors.find("clamped") != std::string::npos);
    CHECK(p.material_noise.step == kMapW / 2);                             // clamped
    CHECK(p.material_noise.octaves == GenParams{}.material_noise.octaves); // unchanged
    CHECK(p.spawn_waves == GenParams{}.spawn_waves);
}

TEST_CASE("height keys from before #30 are reported as unknown and change nothing") {
    GenParams p;
    std::string errors;
    CHECK_FALSE(
        from_preset("mesa_band = 30\nrange_pct_per_grade = 7\nheight.octaves = 4\n", p, errors));
    CHECK(errors.find("unknown key 'mesa_band'") != std::string::npos);
    CHECK(errors.find("unknown key 'range_pct_per_grade'") != std::string::npos);
    CHECK(errors.find("unknown key 'height.octaves'") != std::string::npos);
    CHECK(p == GenParams{});
}

TEST_CASE("enum names") {
    CHECK(std::string(curve_name(Curve::Quintic)) == "quintic");
    CHECK(std::string(shape_name(Shape::Billowed)) == "billowed");
}
