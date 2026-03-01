#pragma once

#include <string>

struct Args {
  std::string file;
  std::string log_level;
  std::string log_file;
  std::string view_mode;

  Args(int argc, char* argv[]);
};
