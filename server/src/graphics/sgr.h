#pragma once

#include <format>
#include <iterator>

/// @brief SGR (Select Graphic Rendition) escape sequence helpers.
namespace sgr {

/// @brief Reset all attributes.
constexpr auto Reset = "\x1b[0m";
/// @brief Bold attribute.
constexpr auto Bold = "\x1b[1m";
/// @brief Underline attribute.
constexpr auto Underline = "\x1b[4m";
/// @brief Reverse video attribute.
constexpr auto Reverse = "\x1b[7m";
/// @brief Bold + reverse video.
constexpr auto BoldReverse = "\x1b[1;7m";
/// @brief Reset + bold + underline (for highlighted text within reverse regions).
constexpr auto ResetBoldUnderline = "\x1b[0m\x1b[1;4m";
/// @brief Default foreground color.
constexpr auto DefaultFg = "\x1b[39m";
/// @brief Default background color.
constexpr auto DefaultBg = "\x1b[49m";
/// @brief Colon-separated 24-bit foreground color prefix (Kitty placeholder protocol).
constexpr auto FgColorColonPrefix = "\x1b[38:2:";

/// @brief Append cursor-positioning escape sequence to an output string.
/// Row and col are 1-based terminal coordinates.
template <typename Out>
void move_to(Out& out, int row, int col) {
  std::format_to(std::back_inserter(out), "\x1b[{};{}H", row, col);
}

/// @brief Append cursor-positioning to column 1 of a row.
template <typename Out>
void move_to_row(Out& out, int row) {
  std::format_to(std::back_inserter(out), "\x1b[{};1H", row);
}

} // namespace sgr
