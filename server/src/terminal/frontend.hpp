#pragma once

#include <frontend.hpp> // angle brackets to resolve base, not self

class App;
class KeyBindings;

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
  void show_pages(const std::vector<PageSlice>& slices) override;
  void statusline(const std::string& left, const std::string& right) override;
  void show_overlay(const std::string& title, const std::vector<std::string>& lines) override;
  void clear_overlay() override;
  void show_sidebar(const std::vector<std::string>& lines, int highlight_line, int width_cols, bool focused) override;
  void show_link_hints(const std::vector<LinkHintDisplay>& hints) override;
  void write_raw(const char* data, size_t len) override;

  /// @brief Run the terminal event loop.
  /// @param app Application instance.
  /// @param bindings Key bindings for the input handler.
  /// @param scroll_lines Lines per scroll step for the input handler.
  void run(App& app, const KeyBindings& bindings, int scroll_lines = 3);

  /// @brief Set left-side canvas inset (e.g. for sidebar).
  /// Subsequent client_info(), show_pages(), show_link_hints() account for this offset.
  void set_canvas_inset(int left_cols);

  /// @brief Current canvas inset in columns.
  int canvas_inset() const {
    return inset_cols_;
  }

private:
  void query_winsize();

  bool button1_held_ = false;
  unsigned ws_col_ = 0;
  unsigned ws_row_ = 0;
  unsigned ws_xpixel_ = 0;
  unsigned ws_ypixel_ = 0;
  int inset_cols_ = 0; ///< Left-side canvas inset in columns (e.g. sidebar width).
};
