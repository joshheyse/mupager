#pragma once

#include "color_scheme.h"
#include "key_bindings.h"

#include <optional>
#include <string>

struct Config;

/// @brief Render scale strategy for cached page rendering.
enum class RenderScale {
  AUTO,
  NEVER,
  X025,
  X05,
  X1,
  X2,
  X4
};

struct Args {
  std::string file;
  std::string log_level;
  std::string log_file;
  std::string view_mode;
  std::string mode = "terminal"; ///< Frontend mode: "terminal" or "neovim".
  std::string theme = "dark";    ///< Initial theme.
  RenderScale render_scale = RenderScale::AUTO;
  int scroll_lines = 3;                     ///< Lines per scroll step.
  size_t max_page_cache = 64 * 1024 * 1024; ///< Max page cache size in bytes (default 64 MB).
  bool show_stats = false;                  ///< Show cache stats in the statusline.
  ColorScheme colors;                       ///< Color scheme from config.
  std::optional<std::string> config_file;   ///< Explicit config file path (overrides XDG default).
  std::optional<std::string> terminal_fg;   ///< Override for detected terminal foreground.
  std::optional<std::string> terminal_bg;   ///< Override for detected terminal background.
  KeyBindings key_bindings;                 ///< Configurable key bindings.

  Args(int argc, char* argv[]);

  /// @brief Apply config file values for settings not explicitly set via CLI.
  void apply_config(const Config& cfg);

private:
  static constexpr unsigned CLI_VIEW_MODE = 1 << 0;
  static constexpr unsigned CLI_THEME = 1 << 1;
  static constexpr unsigned CLI_RENDER_SCALE = 1 << 2;
  static constexpr unsigned CLI_SCROLL_LINES = 1 << 3;
  static constexpr unsigned CLI_LOG_LEVEL = 1 << 4;
  static constexpr unsigned CLI_LOG_FILE = 1 << 5;
  static constexpr unsigned CLI_SHOW_STATS = 1 << 6;
  static constexpr unsigned CLI_MAX_PAGE_CACHE = 1 << 7;
  unsigned cli_explicit_ = 0; ///< Bitmask of CLI flags explicitly provided.
};
