#include "app.h"
#include "args.h"
#include "terminal_frontend.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
  Args args(argc, argv);

  auto logger = spdlog::basic_logger_mt("mupdf-server", args.log_file, true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::from_str(args.log_level));
  spdlog::flush_every(std::chrono::seconds(1));
  spdlog::info("mupdf-server starting: {}", args.file);

  try {
    auto frontend = std::make_unique<TerminalFrontend>();
    App app(std::move(frontend), args);
    app.run();
  }
  catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    return 1;
  }

  return 0;
}
