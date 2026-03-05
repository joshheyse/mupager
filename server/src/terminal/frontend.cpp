#include "terminal/frontend.hpp"

#include "geometry.hpp"
#include "frontend.hpp"
#include "graphics/kitty.hpp"
#include "graphics/sgr.hpp"
#include "input_event.hpp"
#include "util/stopwatch.hpp"

#include <ncurses.h>
#include <spdlog/spdlog.h>
#include <sys/_types/_wint_t.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>
#include <unistd.h>

#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <format>
#include <iterator>
#include <string>
#include <optional>
#include <vector>

TerminalFrontend::TerminalFrontend() {
  set_escdelay(25);
  initscr();
  raw();
  noecho();
  keypad(stdscr, TRUE);
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr);
  mouseinterval(0);
  curs_set(0);
  refresh();

  query_winsize();
}

TerminalFrontend::~TerminalFrontend() {
  std::string seq = build_image_cleanup_sequence();
  std::fwrite(seq.data(), 1, seq.size(), stdout);
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
      return InputEvent{input::Resize, 0, EventType::Press};
    }
    if (wch == KEY_MOUSE) {
      MEVENT mevent{};
      if (getmouse(&mevent) == OK) {
        unsigned mods = 0;
        if (mevent.bstate & BUTTON_CTRL) {
          mods |= input::ModCtrl;
        }
        if (mevent.bstate & BUTTON_SHIFT) {
          mods |= input::ModShift;
        }
        uint32_t id = 0;
        EventType etype = EventType::Press;
        if (mevent.bstate & BUTTON4_PRESSED) {
          id = input::MouseScrollUp;
        }
#ifdef BUTTON5_PRESSED
        else if (mevent.bstate & BUTTON5_PRESSED) {
          id = input::MouseScrollDn;
        }
#endif
        else if (mevent.bstate & BUTTON1_PRESSED) {
          id = input::MousePress;
          button1_held_ = true;
        }
        else if (mevent.bstate & BUTTON1_RELEASED) {
          id = input::MouseRelease;
          etype = EventType::Release;
          button1_held_ = false;
        }
        else if (mevent.bstate & REPORT_MOUSE_POSITION) {
          if (button1_held_) {
            id = input::MouseDrag;
          }
        }
        if (id != 0) {
          return InputEvent{id, mods, etype, mevent.x, mevent.y};
        }
      }
      return std::nullopt;
    }
    if (wch == KEY_BACKSPACE) {
      return InputEvent{input::Backspace, 0, EventType::Press};
    }
    if (wch == KEY_UP) {
      return InputEvent{input::ArrowUp, 0, EventType::Press};
    }
    if (wch == KEY_DOWN) {
      return InputEvent{input::ArrowDown, 0, EventType::Press};
    }
    if (wch == KEY_PPAGE) {
      return InputEvent{input::PageUp, 0, EventType::Press};
    }
    if (wch == KEY_NPAGE) {
      return InputEvent{input::PageDown, 0, EventType::Press};
    }
    if (wch == KEY_HOME) {
      return InputEvent{input::Home, 0, EventType::Press};
    }
    if (wch == KEY_END) {
      return InputEvent{input::End, 0, EventType::Press};
    }
    if (wch == KEY_LEFT) {
      return InputEvent{input::ArrowLeft, 0, EventType::Press};
    }
    if (wch == KEY_RIGHT) {
      return InputEvent{input::ArrowRight, 0, EventType::Press};
    }
  }

  auto id = static_cast<uint32_t>(wch);
  return InputEvent{id, 0, EventType::Press};
}

void TerminalFrontend::clear() {
  std::string seq = build_image_cleanup_sequence();
  std::fwrite(seq.data(), 1, seq.size(), stdout);
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
  int inset_px = inset_cols_ * cell.width;
  return {
      {static_cast<int>(ws_xpixel_) - inset_px, static_cast<int>(ws_ypixel_)},
      cell,
      static_cast<int>(ws_col_) - inset_cols_,
      static_cast<int>(ws_row_),
  };
}

void TerminalFrontend::set_canvas_inset(int left_cols) {
  inset_cols_ = left_cols;
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
      int dst_col = s.dst.x + inset_cols_;
      int first_cell_row = (s.src.y > 0 && s.img_px_size.height > 0) ? s.src.y * s.img_grid.height / s.img_px_size.height : 0;
      int first_cell_col = (s.src.x > 0 && s.img_px_size.width > 0) ? s.src.x * s.img_grid.width / s.img_px_size.width : 0;
      int visible_cols = (s.dst.width > 0) ? s.dst.width : s.img_grid.width;
      // Emit one row at a time with explicit cursor positioning to avoid
      // \n\r in placeholders resetting the column to 1.
      for (int r = 0; r < s.dst.height; ++r) {
        sgr::move_to(out, s.dst.y + 1 + r, dst_col + 1);
        out += kitty::placeholders(s.image_id, first_cell_row + r, 1, visible_cols, first_cell_col);
      }
    }
  }
  else {
    out += kitty::delete_all_placements();
    for (const auto& s : slices) {
      int dst_col = s.dst.x + inset_cols_;
      sgr::move_to(out, s.dst.y + 1, dst_col + 1);
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
  out.reserve(width + 64);
  sgr::move_to_row(out, ws_row_);
  out += colors_.statusline_fg.sgr_fg();
  out += colors_.statusline_bg.sgr_bg();
  out += padded_left;
  out.append(pad, ' ');
  out += padded_right;
  out += sgr::Reset;

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void TerminalFrontend::show_overlay(const std::string& title, const std::vector<std::string>& lines) {
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

  // Box layout: │ SP content SP │ → content_w + 6 total
  int box_w = max_width + 6;
  int box_h = static_cast<int>(lines.size()) + 2; // +2 for top/bottom border rows
  int start_col = std::max(1, (static_cast<int>(ws_col_) - box_w) / 2 + 1);
  int start_row = std::max(1, (static_cast<int>(ws_row_) - box_h) / 2 + 1);
  int max_start = std::max(1, static_cast<int>(ws_row_) - box_h - 1);
  start_row = std::min(start_row, max_start);

  std::string border_sgr = colors_.overlay_border.sgr_fg() + colors_.overlay_bg.sgr_bg();
  std::string content_sgr = colors_.overlay_fg.sgr_fg() + colors_.overlay_bg.sgr_bg();

  // UTF-8 box-drawing characters
  const char* corner_tl = "\xe2\x95\xad"; // ╭
  const char* corner_tr = "\xe2\x95\xae"; // ╮
  const char* corner_bl = "\xe2\x95\xb0"; // ╰
  const char* corner_br = "\xe2\x95\xaf"; // ╯
  const char* horiz = "\xe2\x94\x80";     // ─
  const char* vert = "\xe2\x94\x82";      // │

  std::string out;
  out.reserve(box_h * (box_w + 128));

  // Top border: ╭─ Title ─────────╮
  int title_w = display_width(title);
  int border_inner = box_w - 2; // between TL and TR
  sgr::move_to(out, start_row, start_col);
  out += border_sgr;
  out += corner_tl;
  if (title_w > 0 && title_w + 4 <= border_inner) {
    // ─ SP Title SP ───...
    out += horiz;
    out += " ";
    out += content_sgr;
    out += title;
    out += border_sgr;
    out += " ";
    for (int i = 0; i < border_inner - title_w - 4; ++i) {
      out += horiz;
    }
    out += horiz;
  }
  else {
    for (int i = 0; i < border_inner; ++i) {
      out += horiz;
    }
  }
  out += corner_tr;
  out += sgr::Reset;

  // Body lines
  for (size_t i = 0; i < lines.size(); ++i) {
    int row = start_row + 1 + static_cast<int>(i);
    sgr::move_to(out, row, start_col);
    out += sgr::Reset;
    out += border_sgr;
    out += vert;
    out += content_sgr;
    int line_w = display_width(lines[i]);
    int right_pad = box_w - 4 - line_w; // V + SP + content + SP + V
    std::format_to(std::back_inserter(out), " {}{}", lines[i], "");
    // Re-apply content colors after line content (may contain embedded ANSI)
    out += content_sgr;
    out.append(std::max(0, right_pad), ' ');
    out += " ";
    out += border_sgr;
    out += vert;
    out += sgr::Reset;
  }

  // Bottom border: ╰──────────────╯
  int bottom_row = start_row + box_h - 1;
  sgr::move_to(out, bottom_row, start_col);
  out += border_sgr;
  out += corner_bl;
  for (int i = 0; i < box_w - 2; ++i) {
    out += horiz;
  }
  out += corner_br;
  out += sgr::Reset;

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
    sgr::move_to_row(out, row + 1);

    if (row < static_cast<int>(lines.size())) {
      int dw = display_width(lines[row]);
      bool is_highlight = (row == highlight_line);

      if (is_highlight) {
        if (focused) {
          if (colors_.sidebar_active_fg.is_default && colors_.sidebar_active_bg.is_default) {
            out += sgr::BoldReverse;
          }
          else {
            out += sgr::Bold;
            out += colors_.sidebar_active_fg.sgr_fg();
            out += colors_.sidebar_active_bg.sgr_bg();
          }
        }
        else {
          out += sgr::Bold;
        }
      }
      else if (!colors_.sidebar_fg.is_default || !colors_.sidebar_bg.is_default) {
        out += colors_.sidebar_fg.sgr_fg();
        out += colors_.sidebar_bg.sgr_bg();
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

      if (is_highlight || !colors_.sidebar_fg.is_default || !colors_.sidebar_bg.is_default) {
        out += sgr::Reset;
      }
    }
    else {
      if (!colors_.sidebar_bg.is_default) {
        out += colors_.sidebar_bg.sgr_bg();
      }
      out.append(content_w, ' ');
      if (!colors_.sidebar_bg.is_default) {
        out += sgr::Reset;
      }
    }

    // Sidebar border separator
    if (!colors_.sidebar_border.is_default) {
      out += colors_.sidebar_border.sgr_fg();
    }
    out += "\xe2\x94\x82"; // │
    if (!colors_.sidebar_border.is_default) {
      out += sgr::Reset;
    }
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
    sgr::move_to(out, hint.row + 1, hint.col + inset_cols_ + 1);
    out += sgr::Bold;
    out += colors_.link_hint_fg.sgr_fg();
    out += colors_.link_hint_bg.sgr_bg();
    out += hint.label;
    out += sgr::Reset;
  }

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void TerminalFrontend::write_raw(const char* data, size_t len) {
  std::fwrite(data, 1, len, stdout);
  std::fflush(stdout);
}
