#pragma once

#include "neovim/rpc_transport.h"

#include <frontend.h> // angle brackets to resolve base, not self

#include <cstdio>
#include <deque>

/// @brief Neovim backend frontend: renders images to /dev/tty, communicates with plugin via msgpack-RPC on stdin/stdout.
class NeovimFrontend : public Frontend {
public:
  NeovimFrontend();
  ~NeovimFrontend() override;

  NeovimFrontend(const NeovimFrontend&) = delete;
  NeovimFrontend& operator=(const NeovimFrontend&) = delete;

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
  std::optional<Command> pop_command() override;

  /// @brief Get the RPC transport for sending notifications.
  RpcTransport& transport() {
    return transport_;
  }

  /// @brief Store a resize command from the plugin.
  /// @param cols Window columns.
  /// @param rows Window rows.
  /// @param offset_row Screen row offset.
  /// @param offset_col Screen column offset.
  void set_window_info(int cols, int rows, int offset_row, int offset_col);

  /// @brief Whether we're in the middle of the quit sequence.
  bool quit_received() const {
    return quit_received_;
  }

private:
  void query_tty_cell_size();
  void tty_write(const char* data, size_t len);
  void tty_write(const std::string& s);

  RpcTransport transport_;
  FILE* tty_ = nullptr; ///< /dev/tty for direct Kitty output.
  int tty_fd_ = -1;     ///< File descriptor for /dev/tty.
  std::deque<Command> command_queue_;

  int win_cols_ = 0;   ///< Neovim window width in columns.
  int win_rows_ = 0;   ///< Neovim window height in rows.
  int offset_row_ = 0; ///< Screen row offset of Neovim window.
  int offset_col_ = 0; ///< Screen column offset of Neovim window.

  int tty_xpixel_ = 0; ///< Terminal pixel width from ioctl.
  int tty_ypixel_ = 0; ///< Terminal pixel height from ioctl.
  int tty_cols_ = 0;   ///< Terminal columns from ioctl.
  int tty_rows_ = 0;   ///< Terminal rows from ioctl.

  std::string tmux_pane_id_; ///< TMUX_PANE identifier (e.g. "%0") for targeting queries.
  int tmux_pane_top_ = 0;    ///< Pane row offset in terminal coordinates (0-based).
  int tmux_pane_left_ = 0;   ///< Pane column offset in terminal coordinates (0-based).
  bool quit_received_ = false;
  bool state_dirty_ = true; ///< Whether to send state_changed on next statusline call.
};
