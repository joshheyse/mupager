#include "document.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <CLI/CLI.hpp>

static std::string default_log_path() {
  const char* state = std::getenv("XDG_STATE_HOME");
  std::filesystem::path dir = state ? std::filesystem::path(state) : std::filesystem::path(std::getenv("HOME")) / ".local" / "state";
  dir /= "mupdf-nvim";
  std::filesystem::create_directories(dir);
  return (dir / "mupdf-server.log").string();
}

int main(int argc, char* argv[]) {
  CLI::App app{"mupdf-server - terminal document viewer"};

  std::string file;
  app.add_option("file", file, "Document to open")->required();

  std::string log_level = "info";
  app.add_option("--log-level", log_level, "Log level (trace, debug, info, warn, error, critical)");

  std::string log_file = default_log_path();
  app.add_option("--log-file", log_file, "Log file path");

  CLI11_PARSE(app, argc, argv);

  auto logger = spdlog::basic_logger_mt("mupdf-server", log_file, true);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::from_str(log_level));
  spdlog::info("mupdf-server starting: {}", file);

  try {
    Document doc(file);
    spdlog::info("opened document: {} pages", doc.page_count());
  }
  catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    return 1;
  }

  return 0;
}
