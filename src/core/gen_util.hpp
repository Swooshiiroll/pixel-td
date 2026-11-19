#pragma once

// Internal helpers shared by the map generator's stages (not part of the public core API).

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/grid.hpp"
#include "core/rng.hpp"

namespace pixeltd::gen {

// Uniform in [lo, hi], inclusive.
inline int rand_range(Rng& rng, int lo, int hi) {
    return lo + static_cast<int>(rng.next_below(static_cast<std::uint32_t>(hi - lo + 1)));
}

// The 4 neighbours, always in this order, so every search visits cells in the same order.
inline constexpr std::array<Cell, 4> kDirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// Breadth-first steps over 4-connected cells where passable(cell) holds, from `sources`.
// Unreached cells stay -1. The queue order is fixed, so results are the same everywhere.
template <class Passable>
void bfs(Grid<int>& dist, const std::vector<Cell>& sources, Passable passable) {
    dist.fill(-1);
    std::vector<Cell> queue;
    queue.reserve(static_cast<std::size_t>(dist.width() * dist.height()));
    for (const Cell s : sources) {
        if (dist.at(s) < 0) {
            dist.at(s) = 0;
            queue.push_back(s);
        }
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Cell c = queue[head];
        const int next = dist.at(c) + 1;
        for (const Cell d : kDirs) {
            const Cell n{c.x + d.x, c.y + d.y};
            if (dist.in_bounds(n) && dist.at(n) < 0 && passable(n)) {
                dist.at(n) = next;
                queue.push_back(n);
            }
        }
    }
}

// Adds integer value noise to `field`: random lattice values in [0, 255] every `step` cells,
// blended with a smoothstep curve, times `weight`. Integer-only, so every platform agrees.
inline void add_value_noise(Grid<int>& field, Rng& rng, int step, int weight) {
    const int lw = field.width() / step + 2;
    const int lh = field.height() / step + 2;
    std::vector<int> lattice(static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh));
    for (int& v : lattice) {
        v = static_cast<int>(rng.next_below(256u));
    }
    // Smoothstep weight for offset f within a step, in [0, 256]: 256 * (3t^2 - 2t^3), t = f/step.
    std::vector<int> ease(static_cast<std::size_t>(step));
    for (int f = 0; f < step; ++f) {
        const long long t2 = static_cast<long long>(f) * f;
        const long long num = 256LL * (3LL * t2 * step - 2LL * t2 * f);
        ease[static_cast<std::size_t>(f)] =
            static_cast<int>(num / (static_cast<long long>(step) * step * step));
    }
    const auto lat = [&](int gx, int gy) {
        return lattice[static_cast<std::size_t>(gy) * static_cast<std::size_t>(lw) +
                       static_cast<std::size_t>(gx)];
    };
    for (int y = 0; y < field.height(); ++y) {
        const int gy = y / step;
        const int wy = ease[static_cast<std::size_t>(y % step)];
        for (int x = 0; x < field.width(); ++x) {
            const int gx = x / step;
            const int wx = ease[static_cast<std::size_t>(x % step)];
            const int top = lat(gx, gy) * (256 - wx) + lat(gx + 1, gy) * wx;
            const int bottom = lat(gx, gy + 1) * (256 - wx) + lat(gx + 1, gy + 1) * wx;
            field.at(x, y) += weight * ((top * (256 - wy) + bottom * wy) >> 16);
        }
    }
}

} // namespace pixeltd::gen
