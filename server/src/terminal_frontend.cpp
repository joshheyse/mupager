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
#include <format>
#include <iterator>
#include <stdexcept>

TerminalFrontend::TerminalFrontend() {
  in_tmux_ = std::getenv("TMUX") != nullptr;

  set_escdelay(25);
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

  if (rc == KEY_CODE_YES) {
    if (wch == KEY_RESIZE) {
      query_winsize();
      return InputEvent{input::RESIZE, 0, EventType::PRESS};
    }
    if (wch == KEY_BACKSPACE) {
      return InputEvent{input::BACKSPACE, 0, EventType::PRESS};
    }
    if (wch == KEY_UP) {
      return InputEvent{input::ARROW_UP, 0, EventType::PRESS};
    }
    if (wch == KEY_DOWN) {
      return InputEvent{input::ARROW_DOWN, 0, EventType::PRESS};
    }
    if (wch == KEY_PPAGE) {
      return InputEvent{input::PAGE_UP, 0, EventType::PRESS};
    }
    if (wch == KEY_NPAGE) {
      return InputEvent{input::PAGE_DOWN, 0, EventType::PRESS};
    }
    if (wch == KEY_HOME) {
      return InputEvent{input::HOME, 0, EventType::PRESS};
    }
    if (wch == KEY_END) {
      return InputEvent{input::END, 0, EventType::PRESS};
    }
    if (wch == KEY_LEFT) {
      return InputEvent{input::ARROW_LEFT, 0, EventType::PRESS};
    }
    if (wch == KEY_RIGHT) {
      return InputEvent{input::ARROW_RIGHT, 0, EventType::PRESS};
    }
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
    Stopwatch sw(std::format("fwrite {}KB", seq.size() / 1024));
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
  // Each slice needs ~16 bytes for cursor escape + variable payload per row.
  // In tmux mode, each row emits a cursor escape + placeholder string.
  int total_rows = 0;
  for (const auto& s : slices) {
    total_rows += s.dst.height;
  }
  out.reserve(total_rows * 128);

  if (in_tmux_) {
    erase();
    for (const auto& s : slices) {
      int first_cell_row = (s.src.y > 0 && s.img_px_size.height > 0) ? s.src.y * s.img_grid.height / s.img_px_size.height : 0;
      int first_cell_col = (s.src.x > 0 && s.img_px_size.width > 0) ? s.src.x * s.img_grid.width / s.img_px_size.width : 0;
      int visible_cols = (s.dst.width > 0) ? s.dst.width : s.img_grid.width;
      // Emit one row at a time with explicit cursor positioning to avoid
      // \n\r in placeholders resetting the column to 1.
      for (int r = 0; r < s.dst.height; ++r) {
        std::format_to(std::back_inserter(out), "\x1b[{};{}H", s.dst.y + 1 + r, s.dst.x + 1);
        out += kitty::placeholders(s.image_id, first_cell_row + r, 1, visible_cols, first_cell_col);
      }
    }
  }
  else {
    out += kitty::delete_all_placements();
    for (const auto& s : slices) {
      std::format_to(std::back_inserter(out), "\x1b[{};{}H", s.dst.y + 1, s.dst.x + 1);
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

/// Count display columns, skipping ANSI escape sequences.
/// Assumes all non-ASCII codepoints are 1 column wide.
static int display_width(const std::string& s) {
  int w = 0;
  for (size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i]);
    // Skip ANSI escape sequences: ESC [ ... final_byte
    if (c == 0x1B && i + 1 < s.size() && s[i + 1] == '[') {
      i += 2;
      while (i < s.size() && static_cast<unsigned char>(s[i]) < 0x40) {
        ++i;
      }
      if (i < s.size()) {
        ++i; // skip final byte
      }
      continue;
    }
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
  auto padded_left = std::format(" {}", left);
  auto padded_right = std::format("{} ", right);
  int content_len = display_width(padded_left) + display_width(padded_right);
  int pad = std::max(0, width - content_len);

  std::string out;
  out.reserve(width + 32);
  // Move cursor to last row, column 1
  std::format_to(std::back_inserter(out), "\x1b[{};1H", ws_row_);
  // Enable reverse video
  out += "\x1b[7m";
  out += padded_left;
  out.append(pad, ' ');
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

  // In direct Kitty mode, images sit on a layer above text.
  // Remove placements so the overlay text is visible.
  if (!in_tmux_) {
    std::string del = kitty::delete_all_placements();
    std::fwrite(del.data(), 1, del.size(), stdout);
    std::fflush(stdout);
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
  // Ensure at least 1 row gap above the status bar (last row)
  int max_start = std::max(1, static_cast<int>(ws_row_) - box_h - 1);
  start_row = std::min(start_row, max_start);

  std::string out;
  // Each row: ~16 bytes cursor escape + ~8 bytes SGR + box_w content + ~4 bytes reset
  out.reserve(box_h * (box_w + 32));

  // Top blank line
  std::format_to(std::back_inserter(out), "\x1b[{};{}H", start_row, start_col);
  out += "\x1b[7m";
  out.append(box_w, ' ');
  out += "\x1b[0m";

  // Content lines
  for (size_t i = 0; i < lines.size(); ++i) {
    int row = start_row + 1 + static_cast<int>(i);
    std::format_to(std::back_inserter(out), "\x1b[{};{}H", row, start_col);
    out += "\x1b[7m";
    int line_w = display_width(lines[i]);
    int right_pad = box_w - 2 - line_w;
    std::format_to(std::back_inserter(out), "  {}{:>{}}", lines[i], "", std::max(0, right_pad));
    out += "\x1b[0m";
  }

  // Bottom blank line
  int bottom_row = start_row + box_h - 1;
  std::format_to(std::back_inserter(out), "\x1b[{};{}H", bottom_row, start_col);
  out += "\x1b[7m";
  out.append(box_w, ' ');
  out += "\x1b[0m";

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void TerminalFrontend::clear_overlay() {
  // Erase text cells so the overlay disappears before images are re-placed.
  erase();
  refresh();
}

void TerminalFrontend::show_sidebar(const std::vector<std::string>& lines, int highlight_line, int width_cols, bool focused) {
  if (ws_row_ == 0 || ws_col_ == 0) {
    return;
  }

  int content_w = width_cols - 1;                   // Reserve 1 col for separator
  int visible_rows = static_cast<int>(ws_row_) - 1; // Exclude status bar

  std::string out;
  out.reserve(visible_rows * (width_cols + 32));

  for (int row = 0; row < visible_rows; ++row) {
    std::format_to(std::back_inserter(out), "\x1b[{};1H", row + 1);

    if (row < static_cast<int>(lines.size())) {
      int dw = display_width(lines[row]);
      bool is_highlight = (row == highlight_line);

      if (is_highlight) {
        if (focused) {
          out += "\x1b[1;7m"; // Bold + reverse
        }
        else {
          out += "\x1b[1m"; // Bold only
        }
      }

      if (dw <= content_w) {
        out += lines[row];
        out.append(content_w - dw, ' ');
      }
      else {
        // Truncate to content_w (approximate: just take first content_w bytes of visible chars)
        int cols = 0;
        size_t pos = 0;
        std::string truncated;
        while (pos < lines[row].size() && cols < content_w) {
          auto c = static_cast<unsigned char>(lines[row][pos]);
          if (c == 0x1B && pos + 1 < lines[row].size() && lines[row][pos + 1] == '[') {
            size_t start = pos;
            pos += 2;
            while (pos < lines[row].size() && static_cast<unsigned char>(lines[row][pos]) < 0x40) {
              ++pos;
            }
            if (pos < lines[row].size()) {
              ++pos;
            }
            truncated += lines[row].substr(start, pos - start);
            continue;
          }
          size_t char_len = 1;
          if (c >= 0xF0) {
            char_len = 4;
          }
          else if (c >= 0xE0) {
            char_len = 3;
          }
          else if (c >= 0x80) {
            char_len = 2;
          }
          truncated += lines[row].substr(pos, char_len);
          pos += char_len;
          ++cols;
        }
        out += truncated;
        if (cols < content_w) {
          out.append(content_w - cols, ' ');
        }
      }

      if (is_highlight) {
        out += "\x1b[0m";
      }
    }
    else {
      out.append(content_w, ' ');
    }

    out += "\xe2\x94\x82"; // │ separator
  }

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void TerminalFrontend::show_link_hints(const std::vector<LinkHintDisplay>& hints) {
  if (hints.empty()) {
    return;
  }

  // In direct Kitty mode, remove placements so text is visible.
  if (!in_tmux_) {
    std::string del = kitty::delete_all_placements();
    std::fwrite(del.data(), 1, del.size(), stdout);
    std::fflush(stdout);
  }

  std::string out;
  out.reserve(hints.size() * 32);

  for (const auto& hint : hints) {
    // Position cursor (1-based)
    std::format_to(std::back_inserter(out), "\x1b[{};{}H", hint.row + 1, hint.col + 1);
    // Yellow background, black bold text
    out += "\x1b[1;30;43m";
    out += hint.label;
    out += "\x1b[0m";
  }

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void TerminalFrontend::write_raw(const char* data, size_t len) {
  std::fwrite(data, 1, len, stdout);
  std::fflush(stdout);
}

bool TerminalFrontend::supports_image_viewporting() const {
  return !in_tmux_;
}
