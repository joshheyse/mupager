#include "color.h"

#include "sgr.h"

#include <format>

Color Color::rgb(uint8_t r, uint8_t g, uint8_t b) {
  return {r, g, b, false};
}

Color Color::terminal_default() {
  return {0, 0, 0, true};
}

std::optional<Color> Color::parse(const std::string& s) {
  if (s == "default") {
    return terminal_default();
  }
  if (s.size() == 7 && s[0] == '#') {
    auto from_hex = [](char hi, char lo) -> std::optional<uint8_t> {
      auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
          return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
          return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
          return 10 + (c - 'A');
        }
        return -1;
      };
      int h = nibble(hi);
      int l = nibble(lo);
      if (h < 0 || l < 0) {
        return std::nullopt;
      }
      return static_cast<uint8_t>(h * 16 + l);
    };
    auto rv = from_hex(s[1], s[2]);
    auto gv = from_hex(s[3], s[4]);
    auto bv = from_hex(s[5], s[6]);
    if (rv && gv && bv) {
      return Color::rgb(*rv, *gv, *bv);
    }
  }
  return std::nullopt;
}

std::string Color::sgr_fg() const {
  if (is_default) {
    return sgr::DEFAULT_FG;
  }
  return std::format("\x1b[38;2;{};{};{}m", r, g, b);
}

std::string Color::sgr_bg() const {
  if (is_default) {
    return sgr::DEFAULT_BG;
  }
  return std::format("\x1b[48;2;{};{};{}m", r, g, b);
}

float Color::luminance() const {
  return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
}
