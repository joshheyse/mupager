#pragma once

#include <cstdint>
#include <optional>
#include <string>

/// @brief An RGB color or a "terminal default" sentinel.
struct Color {
  uint8_t r = 0;           ///< Red component.
  uint8_t g = 0;           ///< Green component.
  uint8_t b = 0;           ///< Blue component.
  bool is_default = false; ///< True means "use terminal's own color".

  /// @brief Create a concrete RGB color.
  static Color rgb(uint8_t r, uint8_t g, uint8_t b);

  /// @brief Create a "terminal default" color sentinel.
  static Color terminal_default();

  /// @brief Parse a color string: "#RRGGBB" hex or "default".
  /// @return The parsed Color, or nullopt on invalid input.
  static std::optional<Color> parse(const std::string& s);

  /// @brief SGR escape for setting this as foreground color.
  /// Returns "\x1b[38;2;R;G;Bm" for concrete colors, "\x1b[39m" for default.
  std::string sgr_fg() const;

  /// @brief SGR escape for setting this as background color.
  /// Returns "\x1b[48;2;R;G;Bm" for concrete colors, "\x1b[49m" for default.
  std::string sgr_bg() const;

  /// @brief Relative luminance (0.0 = black, 1.0 = white).
  /// Uses sRGB luminance coefficients: 0.2126*R + 0.7152*G + 0.0722*B.
  float luminance() const;

  bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && is_default == o.is_default;
  }
  bool operator!=(const Color& o) const {
    return !(*this == o);
  }
};
