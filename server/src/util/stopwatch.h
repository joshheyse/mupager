#pragma once

#include <spdlog/spdlog.h>

#include <chrono>
#include <string>

/// @brief RAII stopwatch that logs elapsed time on destruction.
class Stopwatch {
public:
  explicit Stopwatch(std::string label)
      : label_(std::move(label))
      , start_(std::chrono::steady_clock::now()) {}

  ~Stopwatch() {
    auto elapsed = std::chrono::steady_clock::now() - start_;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    if (us >= 1000) {
      spdlog::info("{}: {:.1f}ms", label_, us / 1000.0);
    }
    else {
      spdlog::info("{}: {}us", label_, us);
    }
  }

  Stopwatch(const Stopwatch&) = delete;
  Stopwatch& operator=(const Stopwatch&) = delete;

private:
  std::string label_;
  std::chrono::steady_clock::time_point start_;
};
