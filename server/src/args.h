#pragma once

#include <string>

/// @brief Oversample strategy for cached page rendering.
enum class Oversample {
  AUTO,
  NEVER,
  X1,
  X2,
  X4
};

struct Args {
  std::string file;
  std::string log_level;
  std::string log_file;
  std::string view_mode;
  std::string mode = "terminal"; ///< Frontend mode: "terminal" or "neovim".
  Oversample oversample = Oversample::AUTO;
  bool show_stats = false; ///< Show cache stats in the statusline.

  Args(int argc, char* argv[]);
};
