#pragma once

// Internal helpers shared by the map generator's stages (not part of the public core API).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/gen_params.hpp"
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

// --- Value noise --------------------------------------------------------------------------------

// Random values in [0, 255], one every `step` cells, covering a field_w x field_h field.
struct Lattice {
    int step = 1;
    int w = 0;
    int h = 0;
    std::vector<int> v;

    [[nodiscard]] int at(int gx, int gy) const noexcept {
        return v[static_cast<std::size_t>(gy) * static_cast<std::size_t>(w) +
                 static_cast<std::size_t>(gx)];
    }
};

inline Lattice make_lattice(Rng& rng, int field_w, int field_h, int step) {
    Lattice l;
    l.step = step;
    l.w = field_w / step + 2;
    l.h = field_h / step + 2;
    l.v.resize(static_cast<std::size_t>(l.w) * static_cast<std::size_t>(l.h));
    for (int& x : l.v) {
        x = static_cast<int>(rng.next_below(256u));
    }
    return l;
}

// The blend weight, in [0, 256], for each offset f within a step: 256 * curve(f / step).
inline std::vector<int> make_ease(int step, Curve curve) {
    std::vector<int> ease(static_cast<std::size_t>(step));
    const long long s = step;
    for (int i = 0; i < step; ++i) {
        const long long f = i;
        long long num = 0;
        long long den = 1;
        switch (curve) {
        case Curve::Linear:
            num = 256 * f;
            den = s;
            break;
        case Curve::Quintic: // 6t^5 - 15t^4 + 10t^3
            num = 256 * (6 * f * f * f * f * f - 15 * f * f * f * f * s + 10 * f * f * f * s * s);
            den = s * s * s * s * s;
            break;
        case Curve::Smoothstep: // 3t^2 - 2t^3
        default:
            num = 256 * (3 * f * f * s - 2 * f * f * f);
            den = s * s * s;
            break;
        }
        ease[static_cast<std::size_t>(i)] = static_cast<int>(num / den);
    }
    return ease;
}

// The noise value, 0..255, at cell (x, y).
inline int sample(const Lattice& l, const std::vector<int>& ease, int x, int y) noexcept {
    const int gx = x / l.step;
    const int gy = y / l.step;
    const int wx = ease[static_cast<std::size_t>(x % l.step)];
    const int wy = ease[static_cast<std::size_t>(y % l.step)];
    const int top = l.at(gx, gy) * (256 - wx) + l.at(gx + 1, gy) * wx;
    const int bottom = l.at(gx, gy + 1) * (256 - wx) + l.at(gx + 1, gy + 1) * wx;
    return (top * (256 - wy) + bottom * wy) >> 16;
}

[[nodiscard]] constexpr int shape_value(int v, Shape shape) noexcept {
    const int fold = 2 * v - 255 < 0 ? 255 - 2 * v : 2 * v - 255; // |2v - 255|, 0..255
    switch (shape) {
    case Shape::Ridged:
        return 255 - fold;
    case Shape::Billowed:
        return fold;
    case Shape::Normal:
    default:
        return v;
    }
}

// Weights of a layer's octaves, first to last: the first is 4096, then each the previous times
// persistence_pct / 100. With 3 octaves at 50%: 4096, 2048, 1024 (4 : 2 : 1).
inline constexpr int kFirstOctaveWeight = 4096;

// Fills `out` with a noise layer: the weighted sum of its octaves' values (each shaped), sampled
// at domain-warped points if warp is on. Returns the sum of the weights, so out / weight is the
// layer's 0..255 value.
//
// Draws: each octave's lattice in order, then (only if warp_cells > 0) the two warp lattices. With
// one octave, or 3 at 50% lacunarity and persistence, and the smoothstep curve, normal shape and
// no warp, the draws and the maths are exactly those of add_value_noise before #21.
// If `octaves_out` isn't null, the first three octaves' values (0..255) are written there and
// their weights into `weights_out`.
inline int fill_noise(Grid<int>& out, Rng& rng, const NoiseSpec& n,
                      std::array<Grid<int>, 3>* octaves_out = nullptr,
                      std::array<int, 3>* weights_out = nullptr) {
    const int w = out.width();
    const int h = out.height();
    const int count = std::clamp(n.octaves, 1, kMaxOctaves);
    std::array<Lattice, kMaxOctaves> lattices;
    std::array<std::vector<int>, kMaxOctaves> eases;
    std::array<int, kMaxOctaves> weights{};
    int step = std::max(2, n.step);
    int weight = kFirstOctaveWeight;
    int total = 0;
    for (int i = 0; i < count; ++i) {
        const auto k = static_cast<std::size_t>(i);
        lattices[k] = make_lattice(rng, w, h, step);
        eases[k] = make_ease(step, n.curve);
        weights[k] = weight;
        total += weight;
        step = std::max(2, step * n.lacunarity_pct / 100);
        weight = weight * n.persistence_pct / 100;
    }
    const bool warp = n.warp_cells > 0;
    Lattice warp_x;
    Lattice warp_y;
    std::vector<int> warp_ease;
    if (warp) {
        const int ws = std::max(2, n.warp_step);
        warp_x = make_lattice(rng, w, h, ws);
        warp_y = make_lattice(rng, w, h, ws);
        warp_ease = make_ease(ws, Curve::Smoothstep);
    }
    if (weights_out != nullptr) {
        for (std::size_t i = 0; i < weights_out->size(); ++i) {
            (*weights_out)[i] = i < static_cast<std::size_t>(count) ? weights[i] : 0;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sx = x;
            int sy = y;
            if (warp) {
                // Push the sample point by up to warp_cells each way.
                sx = std::clamp(x + (sample(warp_x, warp_ease, x, y) - 128) * n.warp_cells / 128, 0,
                                w - 1);
                sy = std::clamp(y + (sample(warp_y, warp_ease, x, y) - 128) * n.warp_cells / 128, 0,
                                h - 1);
            }
            int sum = 0;
            for (int i = 0; i < count; ++i) {
                const auto k = static_cast<std::size_t>(i);
                const int v = shape_value(sample(lattices[k], eases[k], sx, sy), n.shape);
                sum += weights[k] * v;
                if (octaves_out != nullptr && k < octaves_out->size()) {
                    (*octaves_out)[k].at(x, y) = v;
                }
            }
            out.at(x, y) = sum;
        }
    }
    return total;
}

// The legacy noise (before #21): adds integer value noise to `field`, with random lattice values
// in [0, 255] every `step` cells, blended with a smoothstep curve, times `weight`. Kept as the
// reference the tests compare fill_noise's defaults against.
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
