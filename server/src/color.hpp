#pragma once

#include <cstdint>
#include <optional>
#include <string>

/// @brief An RGB color, a 256-color index, or a "terminal default" sentinel.
struct Color {
  uint8_t r = 0;           ///< Red component.
  uint8_t g = 0;           ///< Green component.
  uint8_t b = 0;           ///< Blue component.
  uint8_t index = 0;       ///< 256-color palette index.
  bool is_default = false; ///< True means "use terminal's own color".
  bool is_indexed = false; ///< True means use 256-color index instead of RGB.

  /// @brief Create a concrete RGB color.
  static Color rgb(uint8_t r, uint8_t g, uint8_t b);

  /// @brief Create a 256-color indexed color.
  static Color indexed(uint8_t n);

  /// @brief Create a "terminal default" color sentinel.
  static Color terminal_default();

  /// @brief Parse a color string: "#RRGGBB" hex, bare integer for 256-color index, or "default".
  /// @return The parsed Color, or nullopt on invalid input.
  static std::optional<Color> parse(const std::string& s);

  /// @brief SGR escape for setting this as foreground color.
  /// Returns "\x1b[38;5;Nm" for indexed, "\x1b[38;2;R;G;Bm" for RGB, "\x1b[39m" for default.
  std::string sgr_fg() const;

  /// @brief SGR escape for setting this as background color.
  /// Returns "\x1b[48;5;Nm" for indexed, "\x1b[48;2;R;G;Bm" for RGB, "\x1b[49m" for default.
  std::string sgr_bg() const;

  /// @brief Relative luminance (0.0 = black, 1.0 = white).
  /// Uses sRGB luminance coefficients: 0.2126*R + 0.7152*G + 0.0722*B.
  float luminance() const;

  bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && index == o.index && is_default == o.is_default && is_indexed == o.is_indexed;
  }
  bool operator!=(const Color& o) const {
    return !(*this == o);
  }
};
