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
  else if (s == "0.25") {
    return RenderScale::X025;
  }
  else if (s == "0.5") {
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
  auto* theme_opt = cli.add_option("--theme", theme, "Theme (dark, light)")->check(CLI::IsMember({"dark", "light"}));
  auto* scroll_lines_opt = cli.add_option("--scroll-lines", scroll_lines, "Lines per scroll step");
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
}
