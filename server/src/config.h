#pragma once

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
};

/// @brief Load config from $XDG_CONFIG_HOME/mupager/config.toml.
/// Returns empty Config if file doesn't exist. Throws on parse error.
Config load_config();
