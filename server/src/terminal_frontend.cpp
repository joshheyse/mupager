#include "terminal_frontend.h"

#include "input_event.h"
#include "kitty.h"

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

  spdlog::info("display: pixmap {}x{}, kitty image_id={}", pixmap.width(), pixmap.height(), image_id);

  std::string seq;
  if (in_tmux_) {
    auto [celly, cellx] = cell_size();
    seq = kitty::encode_tmux(pixmap, image_id, static_cast<int>(cellx), static_cast<int>(celly));
  }
  else {
    seq = kitty::encode(pixmap, image_id);
  }

  // Move cursor to top-left before writing image
  std::fputs("\x1b[H", stdout);
  std::fwrite(seq.data(), 1, seq.size(), stdout);
  std::fflush(stdout);

  current_image_id_ = image_id;
}

void TerminalFrontend::statusline(const std::string& /*text*/) {}
