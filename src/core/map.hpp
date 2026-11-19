#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/grid.hpp"

namespace pixeltd {

// The ground's material (design/world.md, Terrain). It sets the hue; it's looks only, for now.
enum class Material : std::uint8_t {
    Dust,
    Gravel,
    Ash,
    Rust,
};
inline constexpr int kMaterials = 4;

// A spawn: where enemies enter the map. 2 x 2 cells, at `cell` (top-left).
struct Spawn {
    Cell cell{};
    bool on_edge = false;  // touches a map edge (otherwise it's inside the map)
    int opens_at_wave = 1; // the first wave its enemies come from

    friend constexpr bool operator==(const Spawn&, const Spawn&) = default;
};

// How the generator got its result; the tests assert the fallback is never needed.
struct GenStats {
    int attempts = 0; // terrain attempts used (1 = the first one worked)
    bool fallback = false;
};

// One run's map, made from its seed (design/world.md, How a map is made).
struct Map {
    std::uint64_t seed = 0;

    // Per cell: the ground's material, and rock formations. The ground is flat (#30). A formation
    // cell holds 1..255, how tall the rock stands there (looks only: shading and shadows); open
    // ground is 0.
    Grid<Material> material{kMapW, kMapH, Material::Dust};
    Grid<std::uint8_t> formation{kMapW, kMapH, 0};

    Rect core_area{};            // cells the core must lie in: the central third
    std::optional<SubCell> core; // top-left subcell of the 9x9-subcell (72 px) core
    std::vector<Spawn> spawns;

    GenStats stats{};
};

} // namespace pixeltd
