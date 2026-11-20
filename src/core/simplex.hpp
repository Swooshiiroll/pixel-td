#pragma once

#include <cstdint>

namespace pixeltd {

// 2D simplex (gradient) noise in fixed point (issue #29), so maps stay identical on every compiler.
// It's built on a skewed triangular lattice with 24 gradient directions, so it has none of value
// noise's square, axis-aligned look.
//
// Coordinates are in 1/65536 lattice units (Q16). The lattice point hashes mix in `seed`, so
// there's no permutation table and no repeat across the map.
// The result is 0..255, centred on 128.
[[nodiscard]] int simplex_q16(std::uint64_t seed, long long x, long long y) noexcept;

// The value at cell (x, y) for features `step` cells across: one lattice unit is `step` cells.
[[nodiscard]] inline int simplex_cell(std::uint64_t seed, int x, int y, int step) noexcept {
    return simplex_q16(seed, static_cast<long long>(x) * 65536 / step,
                       static_cast<long long>(y) * 65536 / step);
}

} // namespace pixeltd
