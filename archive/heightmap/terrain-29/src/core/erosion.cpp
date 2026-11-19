#include "core/erosion.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "core/los.hpp" // isqrt

namespace pixeltd {
namespace {

constexpr long long kOne = kErosionOne;

struct Brush {
    int dx;
    int dy;
    long long share; // of what's eroded, in 1/65536
};

// The cells within `radius`, weighted radius^2 - d^2 (heaviest in the middle), as shares of 65536.
std::vector<Brush> make_brush(int radius) {
    std::vector<Brush> b;
    const int r2 = radius * radius;
    long long total = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int d2 = dx * dx + dy * dy;
            if (d2 < r2) {
                b.push_back({dx, dy, r2 - d2});
                total += r2 - d2;
            }
        }
    }
    for (Brush& c : b) {
        c.share = c.share * kErosionOne / total;
    }
    return b;
}

struct Sample {
    long long h;  // height
    long long gx; // slope along x, per cell
    long long gy; // slope along y, per cell
};

// The height and slope at a point inside the map (Q16 position), blended between the 4 cells
// around it.
Sample sample_at(const Grid<int>& g, long long px, long long py) {
    const int ix = static_cast<int>(px / kOne);
    const int iy = static_cast<int>(py / kOne);
    const long long u = px % kOne;
    const long long v = py % kOne;
    const long long h00 = g.at(ix, iy);
    const long long h10 = g.at(ix + 1, iy);
    const long long h01 = g.at(ix, iy + 1);
    const long long h11 = g.at(ix + 1, iy + 1);
    Sample s{};
    s.gx = ((h10 - h00) * (kOne - v) + (h11 - h01) * v) / kOne;
    s.gy = ((h01 - h00) * (kOne - u) + (h11 - h10) * u) / kOne;
    const long long top = (h00 * (kOne - u) + h10 * u) / kOne;
    const long long bottom = (h01 * (kOne - u) + h11 * u) / kOne;
    s.h = (top * (kOne - v) + bottom * v) / kOne;
    return s;
}

} // namespace

long long erode(Grid<int>& g, Rng& rng, const GenParams& p) {
    const int w = g.width();
    const int h = g.height();
    if (w < 2 || h < 2) {
        return 0;
    }
    const std::vector<Brush> brush = make_brush(std::max(1, p.erosion_radius));
    const long long max_x = static_cast<long long>(w - 1) * kOne;
    const long long max_y = static_cast<long long>(h - 1) * kOne;
    const long long inertia = p.erosion_inertia_pct;
    const long long min_slope = static_cast<long long>(p.erosion_min_slope_pct) * kOne / 100;
    long long lost = 0;

    for (int d = 0; d < p.erosion_droplets; ++d) {
        long long px = rng.next_below(static_cast<std::uint32_t>(max_x));
        long long py = rng.next_below(static_cast<std::uint32_t>(max_y));
        long long dir_x = 0;
        long long dir_y = 0;
        long long speed = kOne;
        long long water = kOne;
        long long sediment = 0;
        for (int step = 0; step < p.erosion_lifetime; ++step) {
            const int ix = static_cast<int>(px / kOne);
            const int iy = static_cast<int>(py / kOne);
            const long long u = px % kOne;
            const long long v = py % kOne;
            const Sample here = sample_at(g, px, py);

            // Turn towards downhill, keeping some of the old direction, then step one cell.
            dir_x = (dir_x * inertia - here.gx * (100 - inertia)) / 100;
            dir_y = (dir_y * inertia - here.gy * (100 - inertia)) / 100;
            const long long len = isqrt(dir_x * dir_x + dir_y * dir_y);
            if (len == 0) {
                break; // a flat pit: it soaks in, and its sediment settles (below)
            }
            dir_x = dir_x * kOne / len;
            dir_y = dir_y * kOne / len;
            const long long nx = px + dir_x;
            const long long ny = py + dir_y;
            if (nx < 0 || ny < 0 || nx >= max_x || ny >= max_y) {
                break; // it runs off the map, sediment and all (counted below)
            }
            const long long dh = sample_at(g, nx, ny).h - here.h; // < 0 downhill

            const long long capacity = std::max(-dh, min_slope) * speed / kOne * water / kOne *
                                       p.erosion_capacity_pct / 100;
            if (sediment > capacity || dh > 0) {
                // Drop sediment: uphill, enough to fill the step (or all of it); otherwise a share
                // of the excess. Spread over the 4 cells around the old point, summing exactly.
                const long long amount = dh > 0
                                             ? std::min(dh, sediment)
                                             : (sediment - capacity) * p.erosion_deposit_pct / 100;
                sediment -= amount;
                const long long a00 = amount * (kOne - u) / kOne * (kOne - v) / kOne;
                const long long a10 = amount * u / kOne * (kOne - v) / kOne;
                const long long a01 = amount * (kOne - u) / kOne * v / kOne;
                g.at(ix, iy) += static_cast<int>(a00);
                g.at(ix + 1, iy) += static_cast<int>(a10);
                g.at(ix, iy + 1) += static_cast<int>(a01);
                g.at(ix + 1, iy + 1) += static_cast<int>(amount - a00 - a10 - a01);
            } else {
                // Pick up a share of the spare capacity, never more than the drop (so it can't dig
                // a pit deeper than where it's going), from the cells around it.
                const long long amount =
                    std::min((capacity - sediment) * p.erosion_erode_pct / 100, -dh);
                for (const Brush& b : brush) {
                    const int bx = ix + b.dx;
                    const int by = iy + b.dy;
                    if (bx < 0 || by < 0 || bx >= w || by >= h) {
                        continue;
                    }
                    int& cell = g.at(bx, by);
                    const long long take =
                        std::min(amount * b.share / kOne, static_cast<long long>(cell));
                    cell -= static_cast<int>(take);
                    sediment += take;
                }
            }

            // Speed up downhill, slow uphill; lose some water.
            const long long speed2 = speed * speed + (-dh) * p.erosion_gravity_pct / 100 * kOne;
            speed = isqrt(std::max(0LL, speed2));
            water = water * (100 - p.erosion_evaporate_pct) / 100;
            px = nx;
            py = ny;
        }
        // It dried up, or stopped in a pit: what it still carries is gone (dumping it on one cell
        // would raise a spike).
        lost += sediment;
    }
    return lost;
}

} // namespace pixeltd
