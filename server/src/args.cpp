#include "args.h"

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

Args::Args(int argc, char* argv[])
    : file{}
    , log_file{default_log_path()}
    , view_mode{"continuous"} {
  CLI::App cli{"mupager - terminal document viewer"};
  cli.add_option("file", file, "Document to open")->required();
  auto* log_opt = cli.add_option("--log-level", log_level, "Log level (trace, debug, info, warn, error, critical)");
  cli.add_option("--log-file", log_file, "Log file path");
  cli.add_option("--view-mode", view_mode, "View mode (continuous, page, page-height, side-by-side)")
      ->check(CLI::IsMember({"continuous", "page", "page-height", "side-by-side"}));
  std::string scale_str = "auto";
  cli.add_option("--render-scale", scale_str, "Render scale (auto, never, 0.25, 0.5, 1, 2, 4)")
      ->check(CLI::IsMember({"auto", "never", "0.25", "0.5", "1", "2", "4"}));
  cli.add_option("--mode", mode, "Frontend mode (terminal, neovim)")->check(CLI::IsMember({"terminal", "neovim"}));
  cli.add_flag("--show-stats", show_stats, "Show cache stats in the statusline");
  try {
    cli.parse(argc, argv);
  }
  catch (const CLI::CallForHelp&) {
    throw std::runtime_error(cli.help());
  }

  if (!log_opt->count()) {
    const char* env = std::getenv("SPDLOG_LEVEL");
    log_level = env ? env : "info";
  }

  if (scale_str == "never") {
    render_scale = RenderScale::NEVER;
  }
  else if (scale_str == "0.25") {
    render_scale = RenderScale::X025;
  }
  else if (scale_str == "0.5") {
    render_scale = RenderScale::X05;
  }
  else if (scale_str == "1") {
    render_scale = RenderScale::X1;
  }
  else if (scale_str == "2") {
    render_scale = RenderScale::X2;
  }
  else if (scale_str == "4") {
    render_scale = RenderScale::X4;
  }
}
