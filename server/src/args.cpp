#include "args.h"

#include <cstdlib>
#include <filesystem>

#include <CLI/CLI.hpp>

static std::string default_log_path() {
  const char* state = std::getenv("XDG_STATE_HOME");
  std::filesystem::path dir = state ? std::filesystem::path(state) : std::filesystem::path(std::getenv("HOME")) / ".local" / "state";
  dir /= "mupdf-nvim";
  std::filesystem::create_directories(dir);
  return (dir / "mupdf-server.log").string();
}

Args::Args(int argc, char* argv[])
    : file{}
    , log_level{"debug"}
    , log_file{default_log_path()} {
  CLI::App cli{"mupdf-server - terminal document viewer"};
  cli.add_option("file", file, "Document to open")->required();
  cli.add_option("--log-level", log_level, "Log level (trace, debug, info, warn, error, critical)");
  cli.add_option("--log-file", log_file, "Log file path");
  cli.parse(argc, argv);
}
