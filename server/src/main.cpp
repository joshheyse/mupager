#include "app.h"
#include "args.h"
#include "config.h"
#include "neovim_frontend.h"
#include "neovim_loop.h"
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
    auto cfg = load_config();
    args->apply_config(cfg);
  }
  catch (const std::exception& e) {
    std::fprintf(stderr, "config error: %s\n", e.what());
    return 1;
  }

  auto logger = spdlog::basic_logger_mt("mupager", args->log_file, true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::from_str(args->log_level));
  spdlog::flush_every(std::chrono::seconds(1));
  spdlog::info("mupager starting: {} (mode: {})", args->file, args->mode);

  try {
    if (args->mode == "neovim") {
      auto frontend = std::make_unique<NeovimFrontend>();
      auto* nvim = frontend.get();
      App app(std::move(frontend), *args);
      run_neovim(app, *nvim);
    }
    else {
      auto frontend = std::make_unique<TerminalFrontend>();
      auto* term = frontend.get();
      App app(std::move(frontend), *args);
      run_terminal(app, *term, args->scroll_lines);
    }
  }
  catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    return 1;
  }

  return 0;
}
