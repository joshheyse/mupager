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
  void show_overlay(const std::string& title, const std::vector<std::string>& lines) override;
  void clear_overlay() override;
  void show_sidebar(const std::vector<std::string>& lines, int highlight_line, int width_cols, bool focused) override;
  void show_link_hints(const std::vector<LinkHintDisplay>& hints) override;
  void write_raw(const char* data, size_t len) override;
  bool supports_image_viewporting() const override;
  void set_color_scheme(const ColorScheme& scheme) override;

private:
  void query_winsize();

  ColorScheme colors_;
  bool in_tmux_ = false;
  uint32_t next_image_id_ = 1;
  std::unordered_set<uint32_t> uploaded_ids_;
  unsigned ws_col_ = 0;
  unsigned ws_row_ = 0;
  unsigned ws_xpixel_ = 0;
  unsigned ws_ypixel_ = 0;
};
