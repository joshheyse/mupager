#pragma once

#include "frontend.h"

#include <cstdint>
#include <unordered_set>

/// @brief ncurses + Kitty graphics terminal frontend.
class TerminalFrontend : public Frontend {
public:
  TerminalFrontend();
  ~TerminalFrontend() override;

  TerminalFrontend(const TerminalFrontend&) = delete;
  TerminalFrontend& operator=(const TerminalFrontend&) = delete;

  std::optional<InputEvent> poll_input(int timeout_ms) override;
  void clear() override;
  ClientInfo client_info() override;
  uint32_t upload_image(const Pixmap& pixmap, int cols, int rows) override;
  void free_image(uint32_t image_id) override;
  void show_pages(const std::vector<PageSlice>& slices) override;
  void statusline(const std::string& left, const std::string& right) override;

private:
  void query_winsize();

  bool in_tmux_ = false;
  uint32_t next_image_id_ = 1;
  std::unordered_set<uint32_t> uploaded_ids_;
  unsigned ws_col_ = 0;
  unsigned ws_row_ = 0;
  unsigned ws_xpixel_ = 0;
  unsigned ws_ypixel_ = 0;
};
