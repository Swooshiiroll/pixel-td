#pragma once

#include <cstdint>

namespace pixeltd {

// SplitMix64 finaliser: turns any 64-bit value into a well-mixed one. Used to derive the
// sub-seeds of a run (map stages, retries) from its one seed, so they never correlate.
constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31u);
}

// 64-bit FNV-1a over values fed in a fixed byte order, so the same data hashes the same on every
// platform. The golden tests use it to pin generated maps across compilers.
class Fnv1a {
  public:
    constexpr void add_byte(std::uint8_t b) noexcept {
        hash_ ^= b;
        hash_ *= 0x100000001b3ull;
    }
    constexpr void add_u32(std::uint32_t v) noexcept {
        for (int i = 0; i < 4; ++i) {
            add_byte(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }
    constexpr void add_int(int v) noexcept { add_u32(static_cast<std::uint32_t>(v)); }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return hash_; }

  private:
    std::uint64_t hash_ = 0xcbf29ce484222325ull;
};

} // namespace pixeltd
