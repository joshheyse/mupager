#pragma once

#include <string>

struct Args {
  std::string file;
  std::string log_level;
  std::string log_file;

  Args(int argc, char* argv[]);
};
