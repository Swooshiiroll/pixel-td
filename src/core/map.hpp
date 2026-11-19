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

    // Per cell: ground height (0-200; rock formations stand at 210-255), material, and whether
    // the cell is part of a rock formation (1) or not (0).
    Grid<std::uint8_t> height{kMapW, kMapH, 0};
    Grid<Material> material{kMapW, kMapH, Material::Dust};
    Grid<std::uint8_t> formation{kMapW, kMapH, 0};

    Rect core_area{};         // where the seed may put the core: the central third
    std::optional<Cell> core; // top-left of the 6x6 core
    std::vector<Spawn> spawns;

    GenStats stats{};
};

} // namespace pixeltd
