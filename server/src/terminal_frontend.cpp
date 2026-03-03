#include "terminal_frontend.h"

#include "input_event.h"
#include "kitty.h"
#include "stopwatch.h"

#include <ncurses.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <unistd.h>

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
      seq = kitty::wrap_tmux(kitty::delete_image(id));
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
      seq = kitty::wrap_tmux(kitty::delete_image(id));
    }
    else {
      seq = kitty::delete_image(id);
    }
    std::fwrite(seq.data(), 1, seq.size(), stdout);
  }
  uploaded_ids_.clear();
  std::fflush(stdout);

  query_winsize();

  // Update ncurses only when the terminal size actually changed.
  // Calling resizeterm() unconditionally pushes KEY_RESIZE on macOS ncurses
  // even when the size hasn't changed, causing an infinite resize loop.
  if (static_cast<int>(ws_row_) != LINES || static_cast<int>(ws_col_) != COLS) {
    resizeterm(ws_row_, ws_col_);
  }
  clearok(curscr, TRUE);
  erase();
  refresh();
}

ClientInfo TerminalFrontend::client_info() {
  query_winsize();
  CellSize cell;
  if (ws_row_ > 0 && ws_col_ > 0) {
    cell = {static_cast<int>(ws_xpixel_ / ws_col_), static_cast<int>(ws_ypixel_ / ws_row_)};
  }
  return {
      {static_cast<int>(ws_xpixel_), static_cast<int>(ws_ypixel_)},
      cell,
      static_cast<int>(ws_col_),
      static_cast<int>(ws_row_),
  };
}

uint32_t TerminalFrontend::upload_image(const Pixmap& pixmap, int cols, int rows) {
  uint32_t image_id = next_image_id_++;

  spdlog::info("upload_image: {}x{} px, kitty image_id={}", pixmap.width(), pixmap.height(), image_id);

  std::string seq;
  {
    Stopwatch sw("kitty transmit encode");
    if (in_tmux_) {
      seq = kitty::wrap_tmux(kitty::transmit(pixmap, image_id, cols, rows));
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
    seq = kitty::wrap_tmux(kitty::delete_image(image_id));
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
      int first_cell_row = (s.src.y > 0 && s.img_px_size.height > 0) ? s.src.y * s.img_grid.height / s.img_px_size.height : 0;
      int first_cell_col = (s.src.x > 0 && s.img_px_size.width > 0) ? s.src.x * s.img_grid.width / s.img_px_size.width : 0;
      int visible_cols = (s.dst.width > 0) ? s.dst.width : s.img_grid.width;
      // Emit one row at a time with explicit cursor positioning to avoid
      // \n\r in placeholders resetting the column to 1.
      for (int r = 0; r < s.dst.height; ++r) {
        out += "\x1b[" + std::to_string(s.dst.y + 1 + r) + ";" + std::to_string(s.dst.x + 1) + "H";
        out += kitty::placeholders(s.image_id, first_cell_row + r, 1, visible_cols, first_cell_col);
      }
    }
  }
  else {
    out += kitty::delete_all_placements();
    for (const auto& s : slices) {
      out += "\x1b[" + std::to_string(s.dst.y + 1) + ";" + std::to_string(s.dst.x + 1) + "H";
      kitty::PlaceCommand cmd;
      cmd.image_id = s.image_id;
      cmd.src_x = s.src.x;
      cmd.src_y = s.src.y;
      cmd.src_w = s.src.width;
      cmd.src_h = s.src.height;
      if (s.dst.width > 0) {
        cmd.columns = s.dst.width;
      }
      if (s.dst.height > 0) {
        cmd.rows = s.dst.height;
      }
      out += cmd.serialize();
    }
  }

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);

  if (in_tmux_) {
    refresh();
  }
}

/// Count display columns (assumes all non-ASCII codepoints are 1 column wide).
static int display_width(const std::string& s) {
  int w = 0;
  for (size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      ++i;
    }
    else if (c < 0xE0) {
      i += 2;
    }
    else if (c < 0xF0) {
      i += 3;
    }
    else {
      i += 4;
    }
    ++w;
  }
  return w;
}

void TerminalFrontend::statusline(const std::string& left, const std::string& right) {
  if (ws_row_ == 0 || ws_col_ == 0) {
    return;
  }

  int width = static_cast<int>(ws_col_);
  // Build content: " {left}{padding}{right} "
  std::string padded_left = " " + left;
  std::string padded_right = right + " ";
  int content_len = display_width(padded_left) + display_width(padded_right);
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

void TerminalFrontend::show_overlay(const std::vector<std::string>& lines) {
  if (ws_row_ == 0 || ws_col_ == 0 || lines.empty()) {
    return;
  }

  // Find widest line by display width
  int max_width = 0;
  for (const auto& line : lines) {
    max_width = std::max(max_width, display_width(line));
  }

  int box_w = max_width + 4;                      // 2 chars padding each side
  int box_h = static_cast<int>(lines.size()) + 2; // 1 row padding top/bottom
  int start_col = std::max(1, (static_cast<int>(ws_col_) - box_w) / 2 + 1);
  int start_row = std::max(1, (static_cast<int>(ws_row_) - box_h) / 2 + 1);

  std::string out;

  // Top blank line
  out += "\x1b[" + std::to_string(start_row) + ";" + std::to_string(start_col) + "H";
  out += "\x1b[7m";
  out += std::string(box_w, ' ');
  out += "\x1b[0m";

  // Content lines
  for (size_t i = 0; i < lines.size(); ++i) {
    int row = start_row + 1 + static_cast<int>(i);
    out += "\x1b[" + std::to_string(row) + ";" + std::to_string(start_col) + "H";
    out += "\x1b[7m";
    int line_w = display_width(lines[i]);
    int right_pad = box_w - 2 - line_w;
    out += "  " + lines[i] + std::string(std::max(0, right_pad), ' ');
    out += "\x1b[0m";
  }

  // Bottom blank line
  int bottom_row = start_row + box_h - 1;
  out += "\x1b[" + std::to_string(bottom_row) + ";" + std::to_string(start_col) + "H";
  out += "\x1b[7m";
  out += std::string(box_w, ' ');
  out += "\x1b[0m";

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void TerminalFrontend::clear_overlay() {
  // No-op — update_viewport() repaints on dismiss.
}

bool TerminalFrontend::supports_image_viewporting() const {
  return !in_tmux_;
}
