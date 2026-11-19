#pragma once

#include <cstdint>

namespace pixeltd {

// Developer overlays (issue #13): views of the generator's noise that replace the terrain while
// on. The O key steps forwards through them, Shift+O backwards. The height overlays (elevation,
// octaves, mesas) went with the heightmap in #30 (archive/heightmap/).
enum class Overlay : std::uint8_t {
    Off,
    Rough,    // formation roughness
    Material, // material noise
};
inline constexpr int kOverlayCount = 3;

// The next view (dir = +1) or the previous one (dir = -1), wrapping round.
[[nodiscard]] constexpr Overlay step_overlay(Overlay o, int dir) noexcept {
    const int i = (static_cast<int>(o) + dir % kOverlayCount + kOverlayCount) % kOverlayCount;
    return static_cast<Overlay>(i);
}

// The name shown in the HUD, e.g. "formation roughness".
[[nodiscard]] const char* overlay_label(Overlay o) noexcept;

// The --overlay name, e.g. "rough" ("off" for Off).
[[nodiscard]] const char* overlay_key(Overlay o) noexcept;

// Parses an --overlay name. False if it isn't one.
[[nodiscard]] bool parse_overlay(const char* text, Overlay& out) noexcept;

} // namespace pixeltd
