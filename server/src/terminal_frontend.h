#pragma once

#include "frontend.h"

struct notcurses;
struct ncplane;

/// @brief notcurses-based terminal frontend.
class TerminalFrontend : public Frontend {
public:
  TerminalFrontend();
  ~TerminalFrontend() override;

  TerminalFrontend(const TerminalFrontend&) = delete;
  TerminalFrontend& operator=(const TerminalFrontend&) = delete;

  std::optional<InputEvent> poll_input(int timeout_ms) override;
  void clear() override;
  std::pair<unsigned, unsigned> pixel_size() override;
  std::pair<unsigned, unsigned> cell_size() override;
  void display() override;
  void statusline(const std::string& text) override;

private:
  struct notcurses* nc_;
  struct ncplane* std_plane_;
};
