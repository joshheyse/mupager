#include "args.h"

#include "config.h"

#include <cstdlib>
#include <filesystem>

#include <CLI/CLI.hpp>

static std::string default_log_path() {
  const char* state = std::getenv("XDG_STATE_HOME");
  std::filesystem::path dir = state ? std::filesystem::path(state) : std::filesystem::path(std::getenv("HOME")) / ".local" / "state";
  dir /= "mupager";
  std::filesystem::create_directories(dir);
  return (dir / "mupager.log").string();
}

static RenderScale parse_render_scale(const std::string& s) {
  if (s == "never") {
    return RenderScale::NEVER;
  }
  else if (s == "0.25" || s == ".25") {
    return RenderScale::X025;
  }
  else if (s == "0.5" || s == ".5") {
    return RenderScale::X05;
  }
  else if (s == "1") {
    return RenderScale::X1;
  }
  else if (s == "2") {
    return RenderScale::X2;
  }
  else if (s == "4") {
    return RenderScale::X4;
  }
  return RenderScale::AUTO;
}

Args::Args(int argc, char* argv[])
    : file{}
    , log_file{default_log_path()}
    , view_mode{"continuous"} {
  CLI::App cli{"mupager - terminal document viewer"};
  cli.add_option("file", file, "Document to open")->required();
  auto* log_level_opt = cli.add_option("--log-level", log_level, "Log level (trace, debug, info, warn, error, critical)");
  auto* log_file_opt = cli.add_option("--log-file", log_file, "Log file path");
  auto* view_mode_opt = cli.add_option("--view-mode", view_mode, "View mode (continuous, page, page-height, side-by-side)")
                            ->check(CLI::IsMember({"continuous", "page", "page-height", "side-by-side"}));
  std::string scale_str = "auto";
  auto* render_scale_opt = cli.add_option("--render-scale", scale_str, "Render scale (auto, never, 0.25, 0.5, 1, 2, 4)")
                               ->check(CLI::IsMember({"auto", "never", "0.25", "0.5", "1", "2", "4"}));
  cli.add_option("--mode", mode, "Frontend mode (terminal, neovim)")->check(CLI::IsMember({"terminal", "neovim"}));
  auto* theme_opt = cli.add_option("--theme", theme, "Theme (dark, light, auto, terminal)")->check(CLI::IsMember({"dark", "light", "auto", "terminal"}));
  auto* scroll_lines_opt = cli.add_option("--scroll-lines", scroll_lines, "Lines per scroll step");
  int max_cache_mb = 64;
  auto* max_page_cache_opt = cli.add_option("--max-page-cache", max_cache_mb, "Max page cache size in MB (default 64)");
  std::string config_str;
  cli.add_option("--config,-c", config_str, "Config file path (overrides XDG default)");
  auto* show_stats_opt = cli.add_flag("--show-stats", show_stats, "Show cache stats in the statusline");
  try {
    cli.parse(argc, argv);
  }
  catch (const CLI::CallForHelp&) {
    throw std::runtime_error(cli.help());
  }

  if (!log_level_opt->count()) {
    const char* env = std::getenv("SPDLOG_LEVEL");
    log_level = env ? env : "info";
  }

  render_scale = parse_render_scale(scale_str);
  max_page_cache = static_cast<size_t>(max_cache_mb) * 1024 * 1024;

  if (!config_str.empty()) {
    config_file = config_str;
  }

  // Track which options were explicitly set on the CLI for apply_config().
  cli_explicit_ = 0;
  if (view_mode_opt->count()) {
    cli_explicit_ |= CLI_VIEW_MODE;
  }
  if (theme_opt->count()) {
    cli_explicit_ |= CLI_THEME;
  }
  if (render_scale_opt->count()) {
    cli_explicit_ |= CLI_RENDER_SCALE;
  }
  if (scroll_lines_opt->count()) {
    cli_explicit_ |= CLI_SCROLL_LINES;
  }
  if (log_level_opt->count()) {
    cli_explicit_ |= CLI_LOG_LEVEL;
  }
  if (log_file_opt->count()) {
    cli_explicit_ |= CLI_LOG_FILE;
  }
  if (show_stats_opt->count()) {
    cli_explicit_ |= CLI_SHOW_STATS;
  }
  if (max_page_cache_opt->count()) {
    cli_explicit_ |= CLI_MAX_PAGE_CACHE;
  }
}

void Args::apply_config(const Config& cfg) {
  if (!(cli_explicit_ & CLI_VIEW_MODE) && cfg.view_mode) {
    view_mode = *cfg.view_mode;
  }
  if (!(cli_explicit_ & CLI_THEME) && cfg.theme) {
    theme = *cfg.theme;
  }
  if (!(cli_explicit_ & CLI_RENDER_SCALE) && cfg.render_scale) {
    render_scale = parse_render_scale(*cfg.render_scale);
  }
  if (!(cli_explicit_ & CLI_SCROLL_LINES) && cfg.scroll_lines) {
    scroll_lines = *cfg.scroll_lines;
  }
  if (!(cli_explicit_ & CLI_LOG_LEVEL) && cfg.log_level) {
    log_level = *cfg.log_level;
  }
  if (!(cli_explicit_ & CLI_LOG_FILE) && cfg.log_file) {
    log_file = *cfg.log_file;
  }
  if (!(cli_explicit_ & CLI_SHOW_STATS) && cfg.show_stats) {
    show_stats = *cfg.show_stats;
  }
  if (!(cli_explicit_ & CLI_MAX_PAGE_CACHE) && cfg.max_page_cache) {
    max_page_cache = static_cast<size_t>(*cfg.max_page_cache) * 1024 * 1024;
  }
  if (cfg.has_colors) {
    colors = cfg.colors;
  }
  if (cfg.terminal_fg) {
    terminal_fg = cfg.terminal_fg;
  }
  if (cfg.terminal_bg) {
    terminal_bg = cfg.terminal_bg;
  }

  // Build key bindings from config
  key_bindings = KeyBindings::defaults();
  if (cfg.has_keys) {
    for (const auto& [action_name, specs] : cfg.keys) {
      auto action = action_from_name(action_name);
      if (!action) {
        continue; // Unknown action names already warned in config parse
      }
      key_bindings.clear(*action);
      for (const auto& spec_str : specs) {
        auto ks = parse_key_spec(spec_str);
        if (ks) {
          key_bindings.bind(*action, *ks);
        }
      }
    }
  }
}
