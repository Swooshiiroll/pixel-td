#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace pixeltd {

// The world is a grid of 12 px cells (design/world.md): 160 x 60 cells, 1920 x 720 px.
inline constexpr int kCellPx = 12;
inline constexpr int kMapW = 160;
inline constexpr int kMapH = 60;
inline constexpr int kMapPxW = kMapW * kCellPx;
inline constexpr int kMapPxH = kMapH * kCellPx;

// Footprints, in cells: light tower 24 px, heavy tower 48 px, core 72 px.
inline constexpr int kLightSize = 2;
inline constexpr int kHeavySize = 4;
inline constexpr int kCoreSize = 6;

struct Cell {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(Cell, Cell) = default;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] constexpr bool contains(Cell c) const noexcept {
        return c.x >= x && c.y >= y && c.x < x + w && c.y < y + h;
    }
    [[nodiscard]] constexpr bool contains(const Rect& r) const noexcept {
        return r.x >= x && r.y >= y && r.x + r.w <= x + w && r.y + r.h <= y + h;
    }

    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

// A fixed-size 2D array stored row by row. Sized once; nothing reallocates afterwards.
template <class T> class Grid {
  public:
    Grid() = default;
    Grid(int w, int h, T value = T{})
        : w_(w), h_(h), cells_(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), value) {}

    [[nodiscard]] int width() const noexcept { return w_; }
    [[nodiscard]] int height() const noexcept { return h_; }

    [[nodiscard]] bool in_bounds(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < w_ && y < h_;
    }
    [[nodiscard]] bool in_bounds(Cell c) const noexcept { return in_bounds(c.x, c.y); }

    [[nodiscard]] std::size_t index(int x, int y) const noexcept {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) +
               static_cast<std::size_t>(x);
    }

    T& at(int x, int y) noexcept { return cells_[index(x, y)]; }
    const T& at(int x, int y) const noexcept { return cells_[index(x, y)]; }
    T& at(Cell c) noexcept { return at(c.x, c.y); }
    const T& at(Cell c) const noexcept { return at(c.x, c.y); }

    void fill(T value) { std::fill(cells_.begin(), cells_.end(), value); }

    [[nodiscard]] const std::vector<T>& cells() const noexcept { return cells_; }

    friend bool operator==(const Grid&, const Grid&) = default;

  private:
    int w_ = 0;
    int h_ = 0;
    std::vector<T> cells_;
};

} // namespace pixeltd
