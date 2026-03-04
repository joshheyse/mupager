#pragma once

#include "color_scheme.h"

#include <optional>
#include <string>

/// @brief Persistent settings loaded from config.toml.
struct Config {
  std::optional<std::string> view_mode;
  std::optional<std::string> theme;
  std::optional<std::string> render_scale;
  std::optional<int> scroll_lines;
  std::optional<std::string> log_level;
  std::optional<std::string> log_file;
  std::optional<bool> show_stats;

  std::optional<std::string> terminal_fg; ///< Override for detected terminal foreground.
  std::optional<std::string> terminal_bg; ///< Override for detected terminal background.
  ColorScheme colors;                     ///< Parsed [colors] section.
  bool has_colors = false;                ///< True if any [colors] keys were set.
};

/// @brief Load config from the given path, or $XDG_CONFIG_HOME/mupager/config.toml if empty.
/// Returns empty Config if file doesn't exist. Throws on parse error.
Config load_config(const std::optional<std::string>& path_override = std::nullopt);
