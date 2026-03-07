#pragma once

#include "color_scheme.hpp"
#include "terminal/key_bindings.hpp"

#include <map>
#include <optional>
#include <string>

struct Config;

struct Args {
  std::string file;
  std::string log_level;
  std::string log_file;
  std::string view_mode;
  std::string mode = "terminal";                 ///< Frontend mode: "terminal" or "neovim".
  std::string theme = "dark";                    ///< Initial theme.
  int scroll_lines = 3;                          ///< Lines per scroll step.
  size_t cache_size = 64 * 1024 * 1024;          ///< Max page cache size in bytes (default 64 MB).
  bool show_stats = false;                       ///< Show cache stats in the statusline.
  bool watch = false;                            ///< Auto-reload document on file changes.
  std::string converter;                         ///< CLI override converter command.
  std::string source_file;                       ///< Original file path before conversion.
  std::map<std::string, std::string> converters; ///< Pattern -> command converter map.
  ColorScheme colors;                            ///< Color scheme from config.
  std::optional<std::string> config_file;        ///< Explicit config file path (overrides XDG default).
  std::optional<std::string> terminal_fg;        ///< Override for detected terminal foreground.
  std::optional<std::string> terminal_bg;        ///< Override for detected terminal background.
  KeyBindings key_bindings;                      ///< Configurable key bindings.
  bool diagnose = false;                         ///< Print diagnostic info and exit.

  Args(int argc, char* argv[]);

  /// @brief Apply config file values for settings not explicitly set via CLI.
  void apply_config(const Config& cfg);

private:
  static constexpr unsigned CliViewMode = 1 << 0;
  static constexpr unsigned CliTheme = 1 << 1;
  static constexpr unsigned CliScrollLines = 1 << 2;
  static constexpr unsigned CliLogLevel = 1 << 3;
  static constexpr unsigned CliLogFile = 1 << 4;
  static constexpr unsigned CliShowStats = 1 << 5;
  static constexpr unsigned CliCacheSize = 1 << 6;
  static constexpr unsigned CliWatch = 1 << 7;
  static constexpr unsigned CliConverter = 1 << 8;
  unsigned cli_explicit_ = 0; ///< Bitmask of CLI flags explicitly provided.
};
