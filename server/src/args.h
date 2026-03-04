#pragma once

#include <string>

/// @brief Render scale strategy for cached page rendering.
enum class RenderScale {
  AUTO,
  NEVER,
  X025,
  X05,
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
  RenderScale render_scale = RenderScale::AUTO;
  bool show_stats = false; ///< Show cache stats in the statusline.

  Args(int argc, char* argv[]);
};
