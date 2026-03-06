#include "args.hpp"

#include "config.hpp"
#include "terminal/key_bindings.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <vector>

#include <CLI/CLI.hpp>

static std::string default_log_path() {
  const char* state = std::getenv("XDG_STATE_HOME");
  const char* home = std::getenv("HOME");
  std::filesystem::path dir = state ? std::filesystem::path(state) : std::filesystem::path(home ? home : "/tmp") / ".local" / "state";
  dir /= "mupager";
  std::filesystem::create_directories(dir);
  return (dir / "mupager.log").string();
}

static RenderScale parse_render_scale(const std::string& s) {
  if (s == "never") {
    return RenderScale::Never;
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
  return RenderScale::Auto;
}

Args::Args(int argc, char* argv[])
    : file{}
    , log_file{default_log_path()}
    , view_mode{"continuous"} {
  CLI::App cli{"mupager - terminal document viewer"};
  cli.set_version_flag("--version", MUPAGER_VERSION);
  cli.add_flag("--diagnose", diagnose, "Print diagnostic info and exit");
  cli.add_option("file", file, "Document to open");
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
  auto* watch_opt = cli.add_flag("--watch", watch, "Auto-reload document on file changes");
  auto* converter_opt = cli.add_option("--converter", converter, "Converter command for this file (%i=input, %o=output, %d=tmpdir)");
  std::string terminal_fg_str;
  cli.add_option("--terminal-fg", terminal_fg_str, "Terminal foreground color (#RRGGBB)");
  std::string terminal_bg_str;
  cli.add_option("--terminal-bg", terminal_bg_str, "Terminal background color (#RRGGBB)");
  std::vector<std::string> converter_patterns;
  cli.add_option("--converter-pattern", converter_patterns, "Converter pattern (glob=command, e.g. '*.md=pandoc %i -o %o')");
  try {
    cli.parse(argc, argv);
  }
  catch (const CLI::CallForHelp&) {
    throw std::runtime_error(cli.help());
  }
  catch (const CLI::CallForVersion&) {
    std::printf("%s\n", MUPAGER_VERSION);
    std::exit(0);
  }

  if (!diagnose && file.empty()) {
    throw CLI::ValidationError("file", "Document path is required (or use --diagnose)");
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
    cli_explicit_ |= CliViewMode;
  }
  if (theme_opt->count()) {
    cli_explicit_ |= CliTheme;
  }
  if (render_scale_opt->count()) {
    cli_explicit_ |= CliRenderScale;
  }
  if (scroll_lines_opt->count()) {
    cli_explicit_ |= CliScrollLines;
  }
  if (log_level_opt->count()) {
    cli_explicit_ |= CliLogLevel;
  }
  if (log_file_opt->count()) {
    cli_explicit_ |= CliLogFile;
  }
  if (show_stats_opt->count()) {
    cli_explicit_ |= CliShowStats;
  }
  if (max_page_cache_opt->count()) {
    cli_explicit_ |= CliMaxPageCache;
  }
  if (watch_opt->count()) {
    cli_explicit_ |= CliWatch;
  }
  if (converter_opt->count()) {
    cli_explicit_ |= CliConverter;
  }

  if (!terminal_fg_str.empty()) {
    terminal_fg = terminal_fg_str;
  }
  if (!terminal_bg_str.empty()) {
    terminal_bg = terminal_bg_str;
  }

  // Parse converter patterns (glob=command) into converters map.
  for (const auto& p : converter_patterns) {
    auto eq = p.find('=');
    if (eq != std::string::npos) {
      converters[p.substr(0, eq)] = p.substr(eq + 1);
    }
  }
}

void Args::apply_config(const Config& cfg) {
  if (!(cli_explicit_ & CliViewMode) && cfg.view_mode) {
    view_mode = *cfg.view_mode;
  }
  if (!(cli_explicit_ & CliTheme) && cfg.theme) {
    theme = *cfg.theme;
  }
  if (!(cli_explicit_ & CliRenderScale) && cfg.render_scale) {
    render_scale = parse_render_scale(*cfg.render_scale);
  }
  if (!(cli_explicit_ & CliScrollLines) && cfg.scroll_lines) {
    scroll_lines = *cfg.scroll_lines;
  }
  if (!(cli_explicit_ & CliLogLevel) && cfg.log_level) {
    log_level = *cfg.log_level;
  }
  if (!(cli_explicit_ & CliLogFile) && cfg.log_file) {
    log_file = *cfg.log_file;
  }
  if (!(cli_explicit_ & CliShowStats) && cfg.show_stats) {
    show_stats = *cfg.show_stats;
  }
  if (!(cli_explicit_ & CliMaxPageCache) && cfg.max_page_cache) {
    max_page_cache = static_cast<size_t>(*cfg.max_page_cache) * 1024 * 1024;
  }
  if (!(cli_explicit_ & CliWatch) && cfg.watch) {
    watch = *cfg.watch;
  }

  // Merge config converters (CLI --converter-pattern values take precedence).
  if (cfg.converters) {
    for (const auto& [pattern, cmd] : *cfg.converters) {
      converters.emplace(pattern, cmd); // emplace won't overwrite CLI patterns
    }
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
      auto* entry = KeyBindings::entry_from_name(action_name);
      if (!entry) {
        continue; // Unknown action names already warned in config parse
      }
      key_bindings.clear(entry);
      for (const auto& spec_str : specs) {
        auto ks = KeySpec::parse(spec_str);
        if (ks) {
          key_bindings.bind(entry, *ks);
        }
      }
    }
  }
}
