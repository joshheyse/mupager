#include "app.hpp"
#include "args.hpp"
#include "color.hpp"
#include "config.hpp"
#include "converter.hpp"
#include "neovim/frontend.hpp"
#include "neovim/loop.hpp"
#include "terminal/frontend.hpp"
#include "terminal/loop.hpp"
#include "terminal/osc_query.hpp"

#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <sys/signal.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

int main(int argc, char* argv[]) {
  // Auto-reap child processes (clipboard helpers, browser openers) to prevent zombies.
  std::signal(SIGCHLD, SIG_IGN);
  std::optional<Args> args;
  try {
    args.emplace(argc, argv);
  }
  catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }

  try {
    auto cfg = load_config(args->config_file);
    args->apply_config(cfg);
  }
  catch (const std::exception& e) {
    std::fprintf(stderr, "config error: %s\n", e.what());
    return 1;
  }

  // Query terminal colors via OSC 10/11 BEFORE ncurses/frontend init.
  // Only query in terminal mode (neovim mode has its own terminal access).
  std::optional<Color> detected_fg;
  std::optional<Color> detected_bg;
  if (args->mode == "terminal" && (args->theme == "auto" || args->theme == "terminal")) {
    auto colors = query_terminal_colors();
    if (colors) {
      detected_fg = colors->first;
      detected_bg = colors->second;
    }
  }

  // Apply config overrides for terminal-fg/terminal-bg
  if (args->terminal_fg) {
    auto c = Color::parse(*args->terminal_fg);
    if (c) {
      detected_fg = *c;
    }
  }
  if (args->terminal_bg) {
    auto c = Color::parse(*args->terminal_bg);
    if (c) {
      detected_bg = *c;
    }
  }

  auto logger = spdlog::basic_logger_mt("mupager", args->log_file, true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::from_str(args->log_level));
  spdlog::flush_every(std::chrono::seconds(1));
  spdlog::info("mupager starting: {} (mode: {})", args->file, args->mode);

  // Run file conversion if a converter matches.
  struct TempFileGuard {
    std::string path;
    ~TempFileGuard() {
      if (!path.empty()) {
        std::filesystem::remove(path);
      }
    }
  };
  TempFileGuard temp_guard;

  auto match = find_converter(args->file, args->converters, args->converter);
  if (match) {
    try {
      auto result = convert(args->file, *match);
      if (result.is_temp) {
        temp_guard.path = result.path;
      }
      args->source_file = args->file;
      args->file = result.path;
      args->converter = result.command;
      spdlog::info("converted: {} -> {} via '{}'", args->source_file, args->file, result.command);
    }
    catch (const std::exception& e) {
      spdlog::error("conversion failed: {}", e.what());
      std::fprintf(stderr, "conversion error: %s\n", e.what());
      return 1;
    }
  }
  else {
    // Check if the file format is natively supported by MuPDF.
    static const std::unordered_set<std::string> NativeExts = {
        ".pdf", ".epub", ".xps",  ".oxps", ".cbz",  ".cbr", ".fb2", ".mobi", ".svg", ".html", ".xhtml", ".htm",
        ".png", ".jpg",  ".jpeg", ".bmp",  ".tiff", ".tif", ".gif", ".pnm",  ".pam", ".pbm",  ".pgm",   ".ppm",
    };
    auto ext = std::filesystem::path(args->file).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!ext.empty() && NativeExts.find(ext) == NativeExts.end()) {
      std::fprintf(stderr, "unsupported file format '%s'\n", ext.c_str());
      std::fprintf(stderr, "hint: configure a converter in config.toml or use --converter:\n");
      std::fprintf(stderr, "  [converters]\n");
      std::fprintf(stderr, "  \"*%s\" = \"<command> %%i -o %%o\"\n", ext.c_str());
      std::fprintf(stderr, "  mupager --converter '<command> %%i -o %%o' %s\n", args->file.c_str());
      return 1;
    }
  }

  if (detected_fg) {
    spdlog::info("detected terminal fg: #{:02x}{:02x}{:02x}", detected_fg->r, detected_fg->g, detected_fg->b);
  }
  if (detected_bg) {
    spdlog::info("detected terminal bg: #{:02x}{:02x}{:02x}", detected_bg->r, detected_bg->g, detected_bg->b);
  }

  try {
    if (args->mode == "neovim") {
      auto frontend = std::make_unique<NeovimFrontend>();
      frontend->set_color_scheme(args->colors);
      auto* nvim = frontend.get();
      App app(std::move(frontend), *args, detected_fg, detected_bg);
      run_neovim(app, *nvim);
    }
    else {
      auto frontend = std::make_unique<TerminalFrontend>();
      frontend->set_color_scheme(args->colors);
      auto* term = frontend.get();
      App app(std::move(frontend), *args, detected_fg, detected_bg);
      run_terminal(app, *term, args->key_bindings, args->scroll_lines);
    }
  }
  catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }

  return 0;
}
