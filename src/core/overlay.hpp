#pragma once

#include <cstdint>

namespace pixeltd {

// Developer overlays (issue #13): views of the generator's noise and of the heightmap that replace
// the terrain while on. The O key steps forwards through them, Shift+O backwards.
enum class Overlay : std::uint8_t {
    Off,
    Elevation,
    Octave1,  // height noise, coarse
    Octave2,  // medium
    Octave3,  // fine
    Height,   // 4 * octave 1 + 2 * octave 2 + octave 3, before stretching and terracing
    Mesa,     // the noise that picks the terraced regions (they're tinted)
    Rough,    // formation roughness
    Material, // material noise
};
inline constexpr int kOverlayCount = 9;

// The next view (dir = +1) or the previous one (dir = -1), wrapping round.
[[nodiscard]] constexpr Overlay step_overlay(Overlay o, int dir) noexcept {
    const int i = (static_cast<int>(o) + dir % kOverlayCount + kOverlayCount) % kOverlayCount;
    return static_cast<Overlay>(i);
}

// The name shown in the HUD, e.g. "height octave 1 (coarse)".
[[nodiscard]] const char* overlay_label(Overlay o) noexcept;

// The --overlay name, e.g. "octave1" ("off" for Off).
[[nodiscard]] const char* overlay_key(Overlay o) noexcept;

// Parses an --overlay name. False if it isn't one.
[[nodiscard]] bool parse_overlay(const char* text, Overlay& out) noexcept;

struct Rgb8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend constexpr bool operator==(Rgb8, Rgb8) = default;
};

// Elevation colours (start values). Ground heights 0..200 run through the stops every 40 units:
// deep blue, teal, green, yellow, orange, red-brown; heights above 200 (rock formations) are pale
// grey-white, brighter the higher they stand.
inline constexpr int kElevationStep = 40;
[[nodiscard]] Rgb8 elevation_colour(int height) noexcept;

// Contour lines: a thin line every 10 height units, a strong one every 50.
inline constexpr int kContourMinor = 10;
inline constexpr int kContourMajor = 50;

// Between two neighbouring sample heights given in 1/16 units: 2 if a major contour lies between
// them, 1 if a minor one does, else 0.
[[nodiscard]] constexpr int contour_level(int a16, int b16) noexcept {
    if (a16 / (kContourMajor * 16) != b16 / (kContourMajor * 16)) {
        return 2;
    }
    return a16 / (kContourMinor * 16) != b16 / (kContourMinor * 16) ? 1 : 0;
}

} // namespace pixeltd
