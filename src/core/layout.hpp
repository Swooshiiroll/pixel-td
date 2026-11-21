#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/gen_params.hpp"
#include "core/sim.hpp"

namespace pixeltd {

// A saved arena setup (#11 phase 2; design/test-arena.md): the map (seed and generator settings)
// and the blocks and test towers on it, so the same test can be run again after a tuning change.
// The tuning numbers aren't part of it (they live in the tuning file). As text:
//
//   seed = 0x1
//   block = light 1155,600
//   tower = heavy 1200,640 first
//   map.mountain_share_pct = 20        (every generator setting, as in a preset, with "map.")

struct Piece {
    SubCell top_left{};
    BlockKind kind = BlockKind::Light;
    bool armed = false; // a test tower
    Targeting targeting = Targeting::First;

    friend constexpr bool operator==(const Piece&, const Piece&) = default;
};

struct Layout {
    std::uint64_t seed = 0;
    GenParams gen{};
    std::vector<Piece> pieces;
};

[[nodiscard]] std::string to_layout(const Layout& l);

// Reads a layout text over `l` (its pieces are replaced; settings it doesn't mention keep their
// values). Each bad line adds to `errors`; the settings are always validated. True if no errors.
bool from_layout(std::string_view text, Layout& l, std::string& errors);

// The blocks and towers standing in a sim, in slot order.
[[nodiscard]] std::vector<Piece> pieces_of(const Sim& sim);

// Places the pieces in order; returns how many couldn't be placed.
int place_pieces(Sim& sim, const std::vector<Piece>& pieces);

} // namespace pixeltd
