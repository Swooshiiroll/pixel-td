#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/grid.hpp"

namespace pixeltd {

// What a cell's ground is (design/world.md, Terrain; #32). Water and wet ground matter for the
// rules: deep water can't be walked or built on; shallow water, mud and marsh can be walked but not
// built on (and will slow enemies once enemies exist).
enum class Ground : std::uint8_t {
    Soil,
    RichSoil,
    Sand,
    Gravel,
    Mud,
    Marsh,
    Shallow, // shallow water: river banks, fords, lake rims
    Deep,    // deep water
};
inline constexpr int kGrounds = 8;

[[nodiscard]] constexpr bool is_water(Ground g) noexcept {
    return g == Ground::Shallow || g == Ground::Deep;
}

// What grows on a cell. Trees block building (not walking or sight); grass and bushes are looks.
enum class Plant : std::uint8_t {
    None,
    Grass,
    Bush,
    Tree,
};
inline constexpr int kPlants = 4;

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

// One run's map, made from its seed (design/world.md, How a map is made). The ground is flat.
struct Map {
    std::uint64_t seed = 0;
    // Per cell: the ground, what grows on it, and rock. A rock cell (a mountain or an outcrop)
    // holds 1..255, how tall the rock stands there (looks only: shading and shadows); open ground
    // is 0. Rock blocks walking, building, sight and direct fire.
    Grid<Ground> ground{kMapW, kMapH, Ground::Soil};
    Grid<Plant> plant{kMapW, kMapH, Plant::None};
    Grid<std::uint8_t> rock{kMapW, kMapH, 0};
    Rect core_area{};            // cells the core must lie in: the central third
    std::optional<SubCell> core; // top-left subcell of the 9x9-subcell (72 px) core
    std::vector<Spawn> spawns;
    GenStats stats{};
};

} // namespace pixeltd
