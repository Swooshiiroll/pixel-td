#include "core/simplex.hpp"

#include <algorithm>
#include <array>

namespace pixeltd {
namespace {

constexpr long long kOne = 65536;        // 1.0 in Q16
constexpr long long kF2 = 23988;         // (sqrt(3) - 1) / 2 in Q16: the skew
constexpr long long kG2 = 13850;         // (3 - sqrt(3)) / 6 in Q16: the unskew
constexpr long long kRadius2 = kOne / 2; // a corner's reach, squared (0.5)

// 24 unit gradients, 15 degrees apart, in Q12. The first quadrant is written out and the rest are
// rotations of it, so the table is symmetric.
constexpr std::array<int, 7> kCos = {4096, 3956, 3547, 2896, 2048, 1060, 0};
struct Grad {
    int x;
    int y;
};
constexpr std::array<Grad, 24> make_grads() {
    std::array<Grad, 24> g{};
    for (int k = 0; k < 24; ++k) {
        const int m = k % 6;
        int gx = kCos[static_cast<std::size_t>(m)];
        int gy = kCos[static_cast<std::size_t>(6 - m)];
        for (int q = 0; q < k / 6; ++q) { // rotate by 90 degrees
            const int t = gx;
            gx = -gy;
            gy = t;
        }
        g[static_cast<std::size_t>(k)] = {gx, gy};
    }
    return g;
}
constexpr std::array<Grad, 24> kGrads = make_grads();

// A lattice point's gradient: a splitmix64-style mix of its coordinates and the seed.
Grad grad_at(std::uint64_t seed, long long i, long long j) noexcept {
    std::uint64_t h = seed ^ (static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ull) ^
                      (static_cast<std::uint64_t>(j) * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return kGrads[static_cast<std::size_t>(h % 24u)];
}

// Floor division by kOne for any sign.
long long floor_unit(long long v) noexcept {
    return v >= 0 ? v / kOne : -((-v + kOne - 1) / kOne);
}

// One corner's contribution in Q16: (r^2 - d^2)^4 * dot(gradient, offset).
long long corner(std::uint64_t seed, long long i, long long j, long long dx,
                 long long dy) noexcept {
    const long long t = kRadius2 - (dx * dx + dy * dy) / kOne; // Q16
    if (t <= 0) {
        return 0;
    }
    const long long t2 = t * t / kOne; // Q16
    const long long t4 = t2 * t2;      // Q32
    const Grad g = grad_at(seed, i, j);
    const long long dot = (g.x * dx + g.y * dy) / 4096; // Q16
    return t4 / kOne * dot / kOne;                      // Q16
}

} // namespace

int simplex_q16(std::uint64_t seed, long long x, long long y) noexcept {
    // Skew into the lattice of triangles and find the cell's first corner.
    const long long s = (x + y) * kF2 / kOne;
    const long long i = floor_unit(x + s);
    const long long j = floor_unit(y + s);
    const long long t = (i + j) * kG2;
    const long long x0 = x - (i * kOne - t);
    const long long y0 = y - (j * kOne - t);
    // Which triangle: the lower or the upper one.
    const long long i1 = x0 > y0 ? 1 : 0;
    const long long j1 = 1 - i1;
    const long long x1 = x0 - i1 * kOne + kG2;
    const long long y1 = y0 - j1 * kOne + kG2;
    const long long x2 = x0 - kOne + 2 * kG2;
    const long long y2 = y0 - kOne + 2 * kG2;
    const long long n = corner(seed, i, j, x0, y0) + corner(seed, i + i1, j + j1, x1, y1) +
                        corner(seed, i + 1, j + 1, x2, y2);
    // n * 70 is about -1..1 in Q16; map it to 0..255.
    const long long v = 128 + n * 70 * 128 / kOne;
    return static_cast<int>(std::clamp(v, 0LL, 255LL));
}

} // namespace pixeltd
