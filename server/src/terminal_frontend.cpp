#include "terminal_frontend.h"

#include "input_event.h"
#include "kitty.h"
#include "stopwatch.h"

#include <ncurses.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

TerminalFrontend::TerminalFrontend() {
  in_tmux_ = std::getenv("TMUX") != nullptr;

  initscr();
  raw();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  refresh(); // Required before get_wch; do it once here to avoid later flicker

  query_winsize();
}

TerminalFrontend::~TerminalFrontend() {
  if (current_image_id_ > 0) {
    std::string seq;
    if (in_tmux_) {
      seq = kitty::delete_image_tmux(current_image_id_);
    }
    else {
      seq = kitty::delete_image(current_image_id_);
    }
    std::fwrite(seq.data(), 1, seq.size(), stdout);
    std::fflush(stdout);
  }
  endwin();
}

void TerminalFrontend::query_winsize() {
  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    ws_col_ = ws.ws_col;
    ws_row_ = ws.ws_row;
    ws_xpixel_ = ws.ws_xpixel;
    ws_ypixel_ = ws.ws_ypixel;
  }
}

std::optional<InputEvent> TerminalFrontend::poll_input(int timeout_ms) {
  wtimeout(stdscr, timeout_ms);

  wint_t wch = 0;
  int rc = get_wch(&wch);

  if (rc == ERR) {
    return std::nullopt;
  }

  if (rc == KEY_CODE_YES && wch == KEY_RESIZE) {
    query_winsize();
    return InputEvent{input::RESIZE, 0, EventType::PRESS};
  }

  auto id = static_cast<uint32_t>(wch);
  return InputEvent{id, 0, EventType::PRESS};
}

void TerminalFrontend::clear() {
  if (current_image_id_ > 0) {
    std::string seq;
    if (in_tmux_) {
      seq = kitty::delete_image_tmux(current_image_id_);
    }
    else {
      seq = kitty::delete_image(current_image_id_);
    }
    std::fwrite(seq.data(), 1, seq.size(), stdout);
    std::fflush(stdout);
    current_image_id_ = 0;
  }
  erase();
  refresh();
}

std::pair<unsigned, unsigned> TerminalFrontend::pixel_size() {
  query_winsize();
  return {ws_ypixel_, ws_xpixel_};
}

std::pair<unsigned, unsigned> TerminalFrontend::cell_size() {
  query_winsize();
  if (ws_row_ == 0 || ws_col_ == 0) {
    return {0, 0};
  }
  return {ws_ypixel_ / ws_row_, ws_xpixel_ / ws_col_};
}

void TerminalFrontend::display(const Pixmap& pixmap) {
  uint32_t image_id = next_image_id_++;

  if (current_image_id_ > 0) {
    std::string del;
    if (in_tmux_) {
      del = kitty::delete_image_tmux(current_image_id_);
    }
    else {
      del = kitty::delete_image(current_image_id_);
    }
    std::fwrite(del.data(), 1, del.size(), stdout);
  }

  spdlog::info("display: transmit pixmap {}x{}, kitty image_id={}", pixmap.width(), pixmap.height(), image_id);

  std::string seq;
  {
    Stopwatch sw("kitty transmit encode");
    if (in_tmux_) {
      auto [celly, cellx] = cell_size();
      cached_cols_ = (pixmap.width() + static_cast<int>(cellx) - 1) / static_cast<int>(cellx);
      cached_rows_ = (pixmap.height() + static_cast<int>(celly) - 1) / static_cast<int>(celly);
      seq = kitty::transmit_tmux(pixmap, image_id, cached_cols_, cached_rows_);
    }
    else {
      seq = kitty::encode(pixmap, image_id, 1);
    }
  }

  {
    Stopwatch sw("fwrite " + std::to_string(seq.size() / 1024) + "KB");
    std::fwrite(seq.data(), 1, seq.size(), stdout);
    std::fflush(stdout);
  }

  current_image_id_ = image_id;
}

void TerminalFrontend::show_region(int x, int y) {
  if (current_image_id_ == 0) {
    return;
  }

  query_winsize();
  int vw = static_cast<int>(ws_xpixel_);
  int vh = static_cast<int>(ws_ypixel_);

  std::string seq;
  if (in_tmux_) {
    auto [celly, cellx] = cell_size();
    int first_row = (celly > 0) ? y / static_cast<int>(celly) : 0;
    int visible_rows = (celly > 0) ? (vh + static_cast<int>(celly) - 1) / static_cast<int>(celly) : 0;
    if (first_row + visible_rows > cached_rows_) {
      visible_rows = cached_rows_ - first_row;
    }
    seq = kitty::placeholders(current_image_id_, first_row, visible_rows, cached_cols_);
  }
  else {
    seq = kitty::place(current_image_id_, x, y, vw, vh);
  }

  Stopwatch sw("show_region");
  std::fputs("\x1b[H", stdout);
  std::fwrite(seq.data(), 1, seq.size(), stdout);
  std::fflush(stdout);
}

void TerminalFrontend::statusline(const std::string& /*text*/) {}
