#pragma once

#include <algorithm>

/// @brief 2D size with width and height.
/// @tparam T Numeric type (int, float, etc.)
template <typename T>
struct Size {
  T width = {};
  T height = {};

  /// @brief Area (width * height).
  T area() const {
    return width * height;
  }

  /// @brief True when both dimensions are positive.
  bool is_positive() const {
    return width > T{} && height > T{};
  }

  bool operator==(const Size& o) const {
    return width == o.width && height == o.height;
  }
  bool operator!=(const Size& o) const {
    return !(*this == o);
  }
};

/// @brief 2D point.
/// @tparam T Numeric type (int, float, etc.)
template <typename T>
struct Point {
  T x = {};
  T y = {};

  bool operator==(const Point& o) const {
    return x == o.x && y == o.y;
  }
  bool operator!=(const Point& o) const {
    return !(*this == o);
  }
};

/// @brief Axis-aligned rectangle stored as origin + size.
/// @tparam T Numeric type (int, float, etc.)
template <typename T>
struct Rect {
  T x = {};
  T y = {};
  T width = {};
  T height = {};

  T left() const {
    return x;
  }
  T top() const {
    return y;
  }
  T right() const {
    return x + width;
  }
  T bottom() const {
    return y + height;
  }

  Size<T> size() const {
    return {width, height};
  }
  Point<T> origin() const {
    return {x, y};
  }

  /// @brief True if this rect contains the given point.
  bool contains(const Point<T>& p) const {
    return p.x >= x && p.x < x + width && p.y >= y && p.y < y + height;
  }

  /// @brief True if this rect overlaps with another rect.
  bool intersects(const Rect& o) const {
    return x < o.x + o.width && x + width > o.x && y < o.y + o.height && y + height > o.y;
  }

  /// @brief Compute the intersection of two rects. Returns a zero-size rect if they don't overlap.
  Rect intersect(const Rect& o) const {
    T ix = std::max(x, o.x);
    T iy = std::max(y, o.y);
    T ir = std::min(right(), o.right());
    T ib = std::min(bottom(), o.bottom());
    if (ix >= ir || iy >= ib) {
      return {};
    }
    return {ix, iy, ir - ix, ib - iy};
  }

  /// @brief Return a copy translated by (dx, dy).
  Rect translated(T dx, T dy) const {
    return {x + dx, y + dy, width, height};
  }

  /// @brief Construct from x, y, width, height.
  static Rect from_xywh(T x, T y, T w, T h) {
    return {x, y, w, h};
  }

  /// @brief Construct from two corner points.
  static Rect from_corners(T x1, T y1, T x2, T y2) {
    return {x1, y1, x2 - x1, y2 - y1};
  }

  /// @brief Construct from origin point and size.
  static Rect from_origin_size(const Point<T>& origin, const Size<T>& size) {
    return {origin.x, origin.y, size.width, size.height};
  }

  bool operator==(const Rect& o) const {
    return x == o.x && y == o.y && width == o.width && height == o.height;
  }
  bool operator!=(const Rect& o) const {
    return !(*this == o);
  }
};

using PixelSize = Size<int>;
using PixelPoint = Point<int>;
using PixelRect = Rect<int>;
using CellSize = Size<int>;
using CellRect = Rect<int>;
using DocSize = Size<float>;

/// @brief Bundles terminal state into one query result.
struct ClientInfo {
  PixelSize pixel; ///< Total terminal area in pixels.
  CellSize cell;   ///< Single cell dimensions in pixels.
  int cols = 0;    ///< Terminal grid columns.
  int rows = 0;    ///< Terminal grid rows.

  /// @brief Viewport pixel size (total minus one cell row for the status bar).
  PixelSize viewport_pixel() const {
    return {pixel.width, pixel.height - cell.height};
  }

  /// @brief True when all dimensions are non-zero.
  bool is_valid() const {
    return cell.is_positive() && pixel.is_positive();
  }
};
