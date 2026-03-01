#pragma once

#include "frontend.h"

#include <cstdint>

/// @brief ncurses + Kitty graphics terminal frontend.
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
  void display(const Pixmap& pixmap) override;
  void statusline(const std::string& text) override;

private:
  void query_winsize();

  bool in_tmux_ = false;
  uint32_t next_image_id_ = 1;
  uint32_t current_image_id_ = 0;
  unsigned ws_col_ = 0;
  unsigned ws_row_ = 0;
  unsigned ws_xpixel_ = 0;
  unsigned ws_ypixel_ = 0;
};
