#include "core/overlay.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace pixeltd {
namespace {

struct OverlayInfo {
    const char* key;
    const char* label;
};

constexpr std::array<OverlayInfo, kOverlayCount> kInfo = {{
    {"off", "terrain"},
    {"elevation", "elevation"},
    {"octave1", "height octave 1 (coarse)"},
    {"octave2", "height octave 2 (medium)"},
    {"octave3", "height octave 3 (fine)"},
    {"height", "combined height noise"},
    {"mesa", "mesa mask"},
    {"rough", "formation roughness"},
    {"material", "material noise"},
}};

// One stop every 20 height units, 0 to 200.
constexpr std::array<Rgb8, 11> kStops = {{
    {24, 40, 110},  // 0 deep blue
    {28, 72, 150},  // 20 blue
    {32, 128, 140}, // 40 teal
    {48, 150, 110}, // 60 sea green
    {70, 160, 70},  // 80 green
    {150, 185, 70}, // 100 yellow-green
    {220, 205, 80}, // 120 yellow
    {228, 175, 62}, // 140 amber
    {225, 140, 50}, // 160 orange
    {195, 98, 44},  // 180 red-orange
    {150, 60, 40},  // 200 red-brown
}};

constexpr std::uint8_t lerp(std::uint8_t a, std::uint8_t b, int f, int n) {
    return static_cast<std::uint8_t>(a + (b - a) * f / n);
}

} // namespace

const char* overlay_label(Overlay o) noexcept {
    return kInfo[static_cast<std::size_t>(o)].label;
}

const char* overlay_key(Overlay o) noexcept {
    return kInfo[static_cast<std::size_t>(o)].key;
}

bool parse_overlay(const char* text, Overlay& out) noexcept {
    for (std::size_t i = 0; i < kInfo.size(); ++i) {
        if (std::strcmp(text, kInfo[i].key) == 0) {
            out = static_cast<Overlay>(i);
            return true;
        }
    }
    return false;
}

Rgb8 elevation_colour(int height) noexcept {
    constexpr int kTop = kElevationStep * (static_cast<int>(kStops.size()) - 1); // 200
    if (height > kTop) {
        // Rock formations (210..255): pale grey-white, brighter the higher.
        const int v = 196 + std::min(40, std::max(0, height - 210) * 40 / 45);
        return {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v),
                static_cast<std::uint8_t>(v + 10)};
    }
    const int h = std::max(0, height);
    const auto i =
        static_cast<std::size_t>(std::min(h / kElevationStep, kTop / kElevationStep - 1));
    const int f = h - static_cast<int>(i) * kElevationStep;
    const Rgb8 a = kStops[i];
    const Rgb8 b = kStops[i + 1];
    return {lerp(a.r, b.r, f, kElevationStep), lerp(a.g, b.g, f, kElevationStep),
            lerp(a.b, b.b, f, kElevationStep)};
}

} // namespace pixeltd
