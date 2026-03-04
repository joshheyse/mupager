#include "config.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>

#include <toml++/toml.hpp>

static std::filesystem::path config_path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  std::filesystem::path dir = xdg ? std::filesystem::path(xdg) : std::filesystem::path(std::getenv("HOME")) / ".config";
  return dir / "mupager" / "config.toml";
}

Config load_config() {
  auto path = config_path();
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

  // Warn about unknown keys for forward compatibility
  for (auto&& [key, val] : tbl) {
    std::string k{key.str()};
    if (k != "view-mode" && k != "theme" && k != "render-scale" && k != "scroll-lines" && k != "log-level" && k != "log-file" && k != "show-stats") {
      spdlog::warn("config: unknown key '{}'", k);
    }
  }

  return cfg;
}
