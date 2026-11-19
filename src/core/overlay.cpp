#include "core/overlay.hpp"

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
    {"rough", "formation roughness"},
    {"material", "material noise"},
}};

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

} // namespace pixeltd
