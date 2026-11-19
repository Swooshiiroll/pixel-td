#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace pixeltd {

// The world sits on a hidden grid of 16 px cells (design/world.md): 320 x 180 cells, 5120 x 2880
// px. The terrain is drawn seamless; the cells are for terrain, line of sight and pathfinding.
inline constexpr int kCellPx = 16;
inline constexpr int kMapW = 320;
inline constexpr int kMapH = 180;
inline constexpr int kMapPxW = kMapW * kCellPx;
inline constexpr int kMapPxH = kMapH * kCellPx;

// Towers and the core snap to a placement subgrid of 8 px subcells, two per cell each way.
inline constexpr int kSubPx = 8;
inline constexpr int kSubPerCell = kCellPx / kSubPx;
inline constexpr int kSubW = kMapW * kSubPerCell; // 640
inline constexpr int kSubH = kMapH * kSubPerCell; // 360

// Footprints, in subcells: light tower 24 px, heavy tower 48 px, core 72 px.
inline constexpr int kLightSub = 3;
inline constexpr int kHeavySub = 6;
inline constexpr int kCoreSub = 9;

// A cell of the hidden grid (16 px).
struct Cell {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(Cell, Cell) = default;
};

// A subcell of the placement subgrid (8 px). A separate type so the two can't be mixed up.
struct SubCell {
    int x = 0;
    int y = 0;

    friend constexpr bool operator==(SubCell, SubCell) = default;
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
