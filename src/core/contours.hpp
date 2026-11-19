#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/grid.hpp"

namespace pixeltd {

// Contour lines for the elevation overlay (issue #25): marching squares over the cell heights,
// with heights blended linearly between cell centres (the same blend the GPU applies to the
// terrain's cell texture), so the lines match the colour bands. A view helper, not part of map
// generation: it uses floats.

struct ContourSegment {
    float x0 = 0.0f; // world pixels
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    bool major = false; // on a multiple of the major interval
};

// The segments, bucketed by the chunk_cells x chunk_cells block of cells each one lies in, so a
// view only draws the chunks it can see.
struct Contours {
    int chunk_cells = 32;
    int chunks_w = 0;
    int chunks_h = 0;
    std::vector<std::vector<ContourSegment>> chunks; // row by row

    [[nodiscard]] const std::vector<ContourSegment>& chunk(int cx, int cy) const {
        return chunks[static_cast<std::size_t>(cy) * static_cast<std::size_t>(chunks_w) +
                      static_cast<std::size_t>(cx)];
    }
    [[nodiscard]] std::size_t total() const noexcept;
};

// A line every `interval` height units; every `major_every` units it's a major line.
[[nodiscard]] Contours build_contours(const Grid<std::uint8_t>& height, int interval = 10,
                                      int major_every = 50, int chunk_cells = 32);

} // namespace pixeltd
