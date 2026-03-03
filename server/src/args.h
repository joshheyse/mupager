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
  Oversample oversample = Oversample::AUTO;

  Args(int argc, char* argv[]);
};
