#include "config.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <unordered_set>

#include <toml++/toml.hpp>

static std::filesystem::path config_path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  std::filesystem::path dir = xdg ? std::filesystem::path(xdg) : std::filesystem::path(std::getenv("HOME")) / ".config";
  return dir / "mupager" / "config.toml";
}

/// @brief Parse a color string from config, logging warnings on invalid values.
static std::optional<Color> parse_color_key(const std::string& key, const std::string& value) {
  auto c = Color::parse(value);
  if (!c) {
    spdlog::warn("config: invalid color value for '{}': '{}'", key, value);
  }
  return c;
}

/// @brief Parse the [colors] table into a ColorScheme.
static void parse_colors_table(const toml::table& tbl, Config& cfg) {
  auto* colors_tbl = tbl["colors"].as_table();
  if (!colors_tbl) {
    return;
  }

  static const std::unordered_set<std::string> KNOWN_COLOR_KEYS = {
      "statusline-fg",     "statusline-bg",       "overlay-fg",     "overlay-bg",    "overlay-border", "sidebar-fg",       "sidebar-bg",
      "sidebar-active-fg", "sidebar-active-bg",   "sidebar-border", "link-hint-fg",  "link-hint-bg",   "search-highlight", "search-highlight-alpha",
      "search-active",     "search-active-alpha", "recolor-dark",   "recolor-light", "recolor-accent",
  };

  for (auto&& [key, val] : *colors_tbl) {
    std::string k{key.str()};
    if (KNOWN_COLOR_KEYS.find(k) == KNOWN_COLOR_KEYS.end()) {
      spdlog::warn("config: unknown [colors] key '{}'", k);
      continue;
    }

    // Handle alpha keys (integer values)
    if (k == "search-highlight-alpha") {
      if (auto v = val.value<int64_t>()) {
        cfg.colors.search_highlight_alpha = static_cast<uint8_t>(std::clamp<int64_t>(*v, 0, 255));
        cfg.has_colors = true;
      }
      else {
        spdlog::warn("config: [colors] '{}' must be an integer", k);
      }
      continue;
    }
    if (k == "search-active-alpha") {
      if (auto v = val.value<int64_t>()) {
        cfg.colors.search_active_alpha = static_cast<uint8_t>(std::clamp<int64_t>(*v, 0, 255));
        cfg.has_colors = true;
      }
      else {
        spdlog::warn("config: [colors] '{}' must be an integer", k);
      }
      continue;
    }

    // Color keys: accept string ("#RRGGBB", "default", "234") or integer (234) values
    std::optional<Color> color;
    if (auto sv = val.value<std::string>()) {
      color = parse_color_key(k, *sv);
    }
    else if (auto iv = val.value<int64_t>()) {
      if (*iv >= 0 && *iv <= 255) {
        color = Color::indexed(static_cast<uint8_t>(*iv));
      }
      else {
        spdlog::warn("config: [colors] '{}' integer must be 0-255, got {}", k, *iv);
      }
    }
    else {
      spdlog::warn("config: [colors] '{}' must be a string or integer", k);
      continue;
    }

    if (!color) {
      continue;
    }

    cfg.has_colors = true;

    if (k == "statusline-fg") {
      cfg.colors.statusline_fg = *color;
    }
    else if (k == "statusline-bg") {
      cfg.colors.statusline_bg = *color;
    }
    else if (k == "overlay-fg") {
      cfg.colors.overlay_fg = *color;
    }
    else if (k == "overlay-bg") {
      cfg.colors.overlay_bg = *color;
    }
    else if (k == "overlay-border") {
      cfg.colors.overlay_border = *color;
    }
    else if (k == "sidebar-fg") {
      cfg.colors.sidebar_fg = *color;
    }
    else if (k == "sidebar-bg") {
      cfg.colors.sidebar_bg = *color;
    }
    else if (k == "sidebar-active-fg") {
      cfg.colors.sidebar_active_fg = *color;
    }
    else if (k == "sidebar-active-bg") {
      cfg.colors.sidebar_active_bg = *color;
    }
    else if (k == "sidebar-border") {
      cfg.colors.sidebar_border = *color;
    }
    else if (k == "link-hint-fg") {
      cfg.colors.link_hint_fg = *color;
    }
    else if (k == "link-hint-bg") {
      cfg.colors.link_hint_bg = *color;
    }
    else if (k == "search-highlight") {
      cfg.colors.search_highlight = *color;
    }
    else if (k == "search-active") {
      cfg.colors.search_active = *color;
    }
    else if (k == "recolor-dark") {
      cfg.colors.recolor_dark = *color;
    }
    else if (k == "recolor-light") {
      cfg.colors.recolor_light = *color;
    }
    else if (k == "recolor-accent") {
      cfg.colors.recolor_accent = *color;
    }
  }
}

/// @brief Parse the [keys] table into the Config keys map.
static void parse_keys_table(const toml::table& tbl, Config& cfg) {
  auto* keys_tbl = tbl["keys"].as_table();
  if (!keys_tbl) {
    return;
  }

  for (auto&& [key, val] : *keys_tbl) {
    std::string action_name{key.str()};
    std::vector<std::string> specs;

    if (auto sv = val.value<std::string>()) {
      specs.push_back(*sv);
    }
    else if (auto* arr = val.as_array()) {
      for (const auto& elem : *arr) {
        if (auto s = elem.value<std::string>()) {
          specs.push_back(*s);
        }
        else {
          spdlog::warn("config: [keys] '{}' array elements must be strings", action_name);
        }
      }
    }
    else {
      spdlog::warn("config: [keys] '{}' must be a string or array of strings", action_name);
      continue;
    }

    cfg.keys[action_name] = std::move(specs);
    cfg.has_keys = true;
  }
}

Config load_config(const std::optional<std::string>& path_override) {
  auto path = path_override ? std::filesystem::path(*path_override) : config_path();
  if (!std::filesystem::exists(path)) {
    spdlog::debug("config: no file at {}", path.string());
    return {};
  }

  spdlog::info("config: loading {}", path.string());
  auto tbl = toml::parse_file(path.string());

  Config cfg;

  if (auto v = tbl["view-mode"].value<std::string>()) {
    cfg.view_mode = *v;
  }
  if (auto v = tbl["theme"].value<std::string>()) {
    cfg.theme = *v;
  }
  if (auto v = tbl["render-scale"].value<std::string>()) {
    cfg.render_scale = *v;
  }
  if (auto v = tbl["scroll-lines"].value<int64_t>()) {
    cfg.scroll_lines = static_cast<int>(*v);
  }
  if (auto v = tbl["log-level"].value<std::string>()) {
    cfg.log_level = *v;
  }
  if (auto v = tbl["log-file"].value<std::string>()) {
    cfg.log_file = *v;
  }
  if (auto v = tbl["show-stats"].value<bool>()) {
    cfg.show_stats = *v;
  }
  if (auto v = tbl["max-page-cache"].value<int64_t>()) {
    cfg.max_page_cache = static_cast<int>(*v);
  }
  if (auto v = tbl["terminal-fg"].value<std::string>()) {
    cfg.terminal_fg = *v;
  }
  if (auto v = tbl["terminal-bg"].value<std::string>()) {
    cfg.terminal_bg = *v;
  }

  parse_colors_table(tbl, cfg);
  parse_keys_table(tbl, cfg);

  // Warn about unknown top-level keys
  static const std::unordered_set<std::string> KNOWN_KEYS = {
      "view-mode",
      "theme",
      "render-scale",
      "scroll-lines",
      "log-level",
      "log-file",
      "show-stats",
      "max-page-cache",
      "terminal-fg",
      "terminal-bg",
      "colors",
      "keys",
  };
  for (auto&& [key, val] : tbl) {
    std::string k{key.str()};
    if (KNOWN_KEYS.find(k) == KNOWN_KEYS.end()) {
      spdlog::warn("config: unknown key '{}'", k);
    }
  }

  return cfg;
}
