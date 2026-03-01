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
  refresh();

  query_winsize();
}

TerminalFrontend::~TerminalFrontend() {
  for (uint32_t id : uploaded_ids_) {
    std::string seq;
    if (in_tmux_) {
      seq = kitty::delete_image_tmux(id);
    }
    else {
      seq = kitty::delete_image(id);
    }
    std::fwrite(seq.data(), 1, seq.size(), stdout);
  }
  std::fflush(stdout);
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
  for (uint32_t id : uploaded_ids_) {
    std::string seq;
    if (in_tmux_) {
      seq = kitty::delete_image_tmux(id);
    }
    else {
      seq = kitty::delete_image(id);
    }
    std::fwrite(seq.data(), 1, seq.size(), stdout);
  }
  uploaded_ids_.clear();
  std::fflush(stdout);

  query_winsize();

  // Update ncurses to the new terminal dimensions and force a full repaint.
  // Avoids endwin()/refresh() which does an alternate-screen-buffer switch
  // that wipes kitty graphics state.
  resizeterm(ws_row_, ws_col_);
  clearok(curscr, TRUE);
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

uint32_t TerminalFrontend::upload_image(const Pixmap& pixmap, int cols, int rows) {
  uint32_t image_id = next_image_id_++;

  spdlog::info("upload_image: {}x{} px, kitty image_id={}", pixmap.width(), pixmap.height(), image_id);

  std::string seq;
  {
    Stopwatch sw("kitty transmit encode");
    if (in_tmux_) {
      seq = kitty::transmit_tmux(pixmap, image_id, cols, rows);
    }
    else {
      seq = kitty::transmit(pixmap, image_id);
    }
  }

  {
    Stopwatch sw("fwrite " + std::to_string(seq.size() / 1024) + "KB");
    std::fwrite(seq.data(), 1, seq.size(), stdout);
    std::fflush(stdout);
  }

  uploaded_ids_.insert(image_id);
  return image_id;
}

void TerminalFrontend::free_image(uint32_t image_id) {
  if (uploaded_ids_.count(image_id) == 0) {
    return;
  }
  std::string seq;
  if (in_tmux_) {
    seq = kitty::delete_image_tmux(image_id);
  }
  else {
    seq = kitty::delete_image(image_id);
  }
  std::fwrite(seq.data(), 1, seq.size(), stdout);
  std::fflush(stdout);
  uploaded_ids_.erase(image_id);
}

void TerminalFrontend::show_pages(const std::vector<PageSlice>& slices) {
  std::string out;

  if (in_tmux_) {
    erase();
    for (const auto& s : slices) {
      out += "\x1b[" + std::to_string(s.dst_row + 1) + ";" + std::to_string(s.dst_col + 1) + "H";
      int first_cell_row = (s.src_y > 0 && s.img_rows > 0) ? s.src_y * s.img_rows / (s.src_h + s.src_y) : 0;
      out += kitty::placeholders(s.image_id, first_cell_row, s.dst_rows, s.img_cols);
    }
  }
  else {
    out += kitty::delete_all_placements();
    for (const auto& s : slices) {
      out += "\x1b[" + std::to_string(s.dst_row + 1) + ";" + std::to_string(s.dst_col + 1) + "H";
      out += kitty::place(s.image_id, s.src_x, s.src_y, s.src_w, s.src_h);
    }
  }

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);

  if (in_tmux_) {
    refresh();
  }
}

void TerminalFrontend::statusline(const std::string& left, const std::string& right) {
  if (ws_row_ == 0 || ws_col_ == 0) {
    return;
  }

  int width = static_cast<int>(ws_col_);
  // Build content: " {left}{padding}{right} "
  std::string padded_left = " " + left;
  std::string padded_right = right + " ";
  int content_len = static_cast<int>(padded_left.size() + padded_right.size());
  int pad = std::max(0, width - content_len);

  std::string out;
  // Move cursor to last row, column 1
  out += "\x1b[" + std::to_string(ws_row_) + ";1H";
  // Enable reverse video
  out += "\x1b[7m";
  out += padded_left;
  out += std::string(pad, ' ');
  out += padded_right;
  // Reset attributes
  out += "\x1b[0m";

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}
