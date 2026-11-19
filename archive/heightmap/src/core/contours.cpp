#include "core/contours.hpp"

#include <algorithm>
#include <array>

namespace pixeltd {
namespace {

struct Point {
    float x;
    float y;
};

// Where level `level` crosses between two samples (heights a, b at points pa, pb).
Point cross(Point pa, Point pb, int a, int b, int level) {
    const float t = static_cast<float>(level - a) / static_cast<float>(b - a);
    return {pa.x + (pb.x - pa.x) * t, pa.y + (pb.y - pa.y) * t};
}

} // namespace

std::size_t Contours::total() const noexcept {
    std::size_t n = 0;
    for (const auto& c : chunks) {
        n += c.size();
    }
    return n;
}

Contours build_contours(const Grid<std::uint8_t>& height, int interval, int major_every,
                        int chunk_cells) {
    Contours out;
    out.chunk_cells = std::max(1, chunk_cells);
    const int w = height.width();
    const int h = height.height();
    out.chunks_w = std::max(1, (w + out.chunk_cells - 1) / out.chunk_cells);
    out.chunks_h = std::max(1, (h + out.chunk_cells - 1) / out.chunk_cells);
    out.chunks.resize(static_cast<std::size_t>(out.chunks_w) *
                      static_cast<std::size_t>(out.chunks_h));
    const int step = std::max(1, interval);
    const int major = std::max(step, major_every);
    const auto centre = [](int c) {
        return static_cast<float>(c * kCellPx) + static_cast<float>(kCellPx) / 2.0f;
    };

    // Each square joins four neighbouring cell centres: a (top-left), b (top-right), c
    // (bottom-right), d (bottom-left). Edges: 0 = a-b, 1 = b-c, 2 = c-d, 3 = d-a.
    for (int y = 0; y + 1 < h; ++y) {
        for (int x = 0; x + 1 < w; ++x) {
            const std::array<int, 4> v = {height.at(x, y), height.at(x + 1, y),
                                          height.at(x + 1, y + 1), height.at(x, y + 1)};
            const int lo = std::min({v[0], v[1], v[2], v[3]});
            const int hi = std::max({v[0], v[1], v[2], v[3]});
            // Levels from lo up to (not including) hi: a corner is above a level if it's higher, so
            // a line never collapses onto a corner that only touches it.
            const int first = std::max(step, (lo + step - 1) / step * step);
            if (first >= hi) {
                continue;
            }
            const std::array<Point, 4> p = {
                Point{centre(x), centre(y)}, Point{centre(x + 1), centre(y)},
                Point{centre(x + 1), centre(y + 1)}, Point{centre(x), centre(y + 1)}};
            auto& bucket = out.chunks[static_cast<std::size_t>(y / out.chunk_cells) *
                                          static_cast<std::size_t>(out.chunks_w) +
                                      static_cast<std::size_t>(x / out.chunk_cells)];
            for (int level = first; level < hi; level += step) {
                // Corners at or above the level.
                int mask = 0;
                for (int i = 0; i < 4; ++i) {
                    mask |= v[static_cast<std::size_t>(i)] > level ? 1 << i : 0;
                }
                if (mask == 0 || mask == 15) {
                    continue;
                }
                const auto edge = [&](int e) {
                    const auto i = static_cast<std::size_t>(e);
                    const auto j = static_cast<std::size_t>((e + 1) % 4);
                    return cross(p[i], p[j], v[i], v[j], level);
                };
                const bool is_major = level % major == 0;
                const auto add = [&](int e0, int e1) {
                    const Point a = edge(e0);
                    const Point b = edge(e1);
                    if (a.x != b.x || a.y != b.y) { // skip a line that collapsed to a point
                        bucket.push_back({a.x, a.y, b.x, b.y, is_major});
                    }
                };
                // The square's centre decides the two ambiguous (saddle) cases.
                const bool centre_above = (v[0] + v[1] + v[2] + v[3]) > 4 * level;
                switch (mask) {
                case 1:
                case 14:
                    add(3, 0);
                    break;
                case 2:
                case 13:
                    add(0, 1);
                    break;
                case 3:
                case 12:
                    add(3, 1);
                    break;
                case 4:
                case 11:
                    add(1, 2);
                    break;
                case 6:
                case 9:
                    add(0, 2);
                    break;
                case 7:
                case 8:
                    add(2, 3);
                    break;
                case 5: // a and c above
                    if (centre_above) {
                        add(0, 1);
                        add(2, 3);
                    } else {
                        add(3, 0);
                        add(1, 2);
                    }
                    break;
                case 10: // b and d above
                    if (centre_above) {
                        add(3, 0);
                        add(1, 2);
                    } else {
                        add(0, 1);
                        add(2, 3);
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }
    return out;
}

} // namespace pixeltd
