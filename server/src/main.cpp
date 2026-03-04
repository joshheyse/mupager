#include "app.h"
#include "args.h"
#include "color.h"
#include "config.h"
#include "neovim_frontend.h"
#include "neovim_loop.h"
#include "osc_query.h"
#include "terminal_frontend.h"
#include "terminal_loop.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
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
    return 1;
  }

  return 0;
}
