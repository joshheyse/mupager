#include "neovim/frontend.h"

#include "graphics/kitty.h"
#include "graphics/sgr.h"

#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

/// @brief Run a tmux display-message command targeting a specific pane and return the trimmed output.
/// @param pane_id The tmux pane target (e.g. "%0"). Empty string uses the active pane.
static std::string tmux_query(const std::string& pane_id, const char* fmt) {
  std::array<char, 128> buf{};
  std::string result;
  std::string cmd = "tmux display-message";
  if (!pane_id.empty()) {
    cmd += " -t " + pane_id;
  }
  cmd += " -p '";
  cmd += fmt;
  cmd += "' 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return {};
  }
  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    result += buf.data();
  }
  pclose(pipe);
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  return result;
}

/// @brief Query the tmux pane TTY path.
static std::string tmux_pane_tty(const std::string& pane_id) {
  return tmux_query(pane_id, "#{pane_tty}");
}

/// @brief Query the tmux pane position in terminal coordinates (0-based).
static std::pair<int, int> tmux_pane_position(const std::string& pane_id) {
  auto result = tmux_query(pane_id, "#{pane_top} #{pane_left}");
  int top = 0;
  int left = 0;
  std::sscanf(result.c_str(), "%d %d", &top, &left);
  return {top, left};
}

/// @brief Wrap raw escape sequences in a single tmux DCS passthrough block.
/// All ESC bytes are doubled per the tmux passthrough protocol.
static std::string tmux_dcs_wrap(const std::string& raw) {
  std::string inner;
  inner.reserve(raw.size() + raw.size() / 4);
  for (char ch : raw) {
    if (ch == '\x1b') {
      inner += '\x1b';
    }
    inner += ch;
  }
  return "\x1bPtmux;" + inner + "\x1b\\";
}

NeovimFrontend::NeovimFrontend()
    : Frontend(1 << 24)
    , transport_(STDIN_FILENO, STDOUT_FILENO) {
  if (in_tmux_) {
    const char* pane_env = std::getenv("TMUX_PANE");
    if (pane_env) {
      tmux_pane_id_ = pane_env;
    }
    spdlog::info("neovim_frontend: tmux pane id: {}", tmux_pane_id_);

    auto pane_tty = tmux_pane_tty(tmux_pane_id_);
    if (!pane_tty.empty()) {
      spdlog::info("neovim_frontend: using tmux pane tty: {}", pane_tty);
      tty_ = std::fopen(pane_tty.c_str(), "w");
    }
    auto [top, left] = tmux_pane_position(tmux_pane_id_);
    tmux_pane_top_ = top;
    tmux_pane_left_ = left;
    spdlog::info("neovim_frontend: tmux pane position top={} left={}", top, left);
  }
  else {
    tty_ = std::fopen("/dev/tty", "w");
  }
  if (!tty_) {
    throw std::runtime_error(std::string("failed to open /dev/tty: ") + std::strerror(errno));
  }
  tty_fd_ = fileno(tty_);
  query_tty_cell_size();
}

NeovimFrontend::~NeovimFrontend() {
  std::string seq = build_image_cleanup_sequence();
  tty_write(seq);
  std::fflush(tty_);

  if (tty_) {
    std::fclose(tty_);
  }
}

void NeovimFrontend::query_tty_cell_size() {
  struct winsize ws{};
  if (ioctl(tty_fd_, TIOCGWINSZ, &ws) == 0) {
    tty_cols_ = ws.ws_col;
    tty_rows_ = ws.ws_row;
    tty_xpixel_ = ws.ws_xpixel;
    tty_ypixel_ = ws.ws_ypixel;
    spdlog::debug("neovim_frontend: tty winsize cols={} rows={} xpixel={} ypixel={}", tty_cols_, tty_rows_, tty_xpixel_, tty_ypixel_);
  }
  if (in_tmux_) {
    auto [top, left] = tmux_pane_position(tmux_pane_id_);
    tmux_pane_top_ = top;
    tmux_pane_left_ = left;
    spdlog::debug("neovim_frontend: tmux pane position top={} left={}", top, left);
  }
}

void NeovimFrontend::tty_write(const char* data, size_t len) {
  std::fwrite(data, 1, len, tty_);
}

void NeovimFrontend::tty_write(const std::string& s) {
  std::fwrite(s.data(), 1, s.size(), tty_);
}

void NeovimFrontend::set_window_info(int cols, int rows, int offset_row, int offset_col) {
  win_cols_ = cols;
  win_rows_ = rows;
  offset_row_ = offset_row;
  offset_col_ = offset_col;
  spdlog::debug("neovim_frontend: window info cols={} rows={} offset_row={} offset_col={}", cols, rows, offset_row, offset_col);
}

std::optional<InputEvent> NeovimFrontend::poll_input(int timeout_ms) {
  // If we have commands queued, return the sentinel immediately
  if (!command_queue_.empty()) {
    return InputEvent{input::RpcCommand, 0, EventType::Press};
  }

  // Poll RPC transport
  auto msg = transport_.poll(timeout_ms);
  if (!msg) {
    return std::nullopt;
  }

  // Handle resize specially — update our stored window info
  if (msg->method == "resize") {
    auto& raw_params = msg->params.get();
    // Unwrap single-element array from msgpack-RPC: [arg] -> arg
    const auto& params = (raw_params.type == msgpack::type::ARRAY && raw_params.via.array.size == 1) ? raw_params.via.array.ptr[0] : raw_params;
    if (params.type == msgpack::type::MAP) {
      cmd::Resize r{};
      auto map = params.via.map;
      for (uint32_t i = 0; i < map.size; ++i) {
        auto& kv = map.ptr[i];
        if (kv.key.type != msgpack::type::STR) {
          continue;
        }
        std::string key = kv.key.as<std::string>();
        if (key == "cols") {
          r.cols = kv.val.as<int>();
        }
        else if (key == "rows") {
          r.rows = kv.val.as<int>();
        }
        else if (key == "offset_row") {
          r.offset_row = kv.val.as<int>();
        }
        else if (key == "offset_col") {
          r.offset_col = kv.val.as<int>();
        }
      }
      set_window_info(r.cols, r.rows, r.offset_row, r.offset_col);
      query_tty_cell_size();
      command_queue_.push_back(r);
    }
    if (msg->type == RpcMessageType::Request) {
      transport_.respond_nil(msg->msgid);
    }
    return InputEvent{input::RpcCommand, 0, EventType::Press};
  }

  // Parse the message into an Command
  auto cmd = RpcTransport::parse_command(msg->method, msg->params.get());
  if (cmd) {
    command_queue_.push_back(*cmd);
    if (msg->type == RpcMessageType::Request) {
      transport_.respond_nil(msg->msgid);
    }
    return InputEvent{input::RpcCommand, 0, EventType::Press};
  }

  // Unknown method — respond with nil for requests
  if (msg->type == RpcMessageType::Request) {
    transport_.respond_nil(msg->msgid);
  }

  return std::nullopt;
}

void NeovimFrontend::clear() {
  std::string seq = build_image_cleanup_sequence();
  tty_write(seq);
  uploaded_ids_.clear();
  std::fflush(tty_);

  query_tty_cell_size();
}

ClientInfo NeovimFrontend::client_info() {
  query_tty_cell_size();

  CellSize cell;
  if (tty_cols_ > 0 && tty_rows_ > 0) {
    cell = {tty_xpixel_ / tty_cols_, tty_ypixel_ / tty_rows_};
  }

  // Use the Neovim window dimensions (not the full terminal)
  int cols = win_cols_ > 0 ? win_cols_ : tty_cols_;
  int rows = win_rows_ > 0 ? win_rows_ : tty_rows_;

  PixelSize pixel = {cols * cell.width, rows * cell.height};

  return {pixel, cell, cols, rows};
}

uint32_t NeovimFrontend::upload_image(const Pixmap& pixmap, int /*cols*/, int /*rows*/) {
  uint32_t image_id = next_image_id_++;

  spdlog::info("neovim upload_image: {}x{} px, kitty image_id={}", pixmap.width(), pixmap.height(), image_id);

  std::string seq = kitty::transmit(pixmap, image_id);
  if (in_tmux_) {
    seq = kitty::wrap_tmux(seq);
  }
  tty_write(seq);
  std::fflush(tty_);

  uploaded_ids_.insert(image_id);
  return image_id;
}

void NeovimFrontend::show_pages(const std::vector<PageSlice>& slices) {
  std::string out;

  if (in_tmux_) {
    // In tmux, cursor positioning goes to the pane, not the terminal.
    // Kitty placement commands go to the terminal via DCS passthrough.
    // We must wrap BOTH cursor positioning and Kitty commands together
    // in a single DCS passthrough so they reach the terminal atomically.
    // Cursor coordinates are in terminal-absolute coordinates.
    std::string raw;
    raw += kitty::delete_all_placements();

    for (const auto& s : slices) {
      int term_row = tmux_pane_top_ + offset_row_ + s.dst.y;
      int term_col = tmux_pane_left_ + offset_col_ + s.dst.x;

      sgr::move_to(raw, term_row + 1, term_col + 1);

      kitty::PlaceCommand cmd;
      cmd.image_id = s.image_id;
      cmd.src_x = s.src.x;
      cmd.src_y = s.src.y;
      cmd.src_w = s.src.width;
      cmd.src_h = s.src.height;
      cmd.z_index = -1073741825;
      if (s.dst.width > 0) {
        cmd.columns = s.dst.width;
      }
      if (s.dst.height > 0) {
        cmd.rows = s.dst.height;
      }
      raw += cmd.serialize();
    }

    out = tmux_dcs_wrap(raw);
  }
  else {
    // Direct Kitty mode — cursor positioning goes straight to the terminal.
    out += kitty::delete_all_placements();

    for (const auto& s : slices) {
      int dst_row = s.dst.y + offset_row_;
      int dst_col = s.dst.x + offset_col_;

      sgr::move_to(out, dst_row + 1, dst_col + 1);

      kitty::PlaceCommand cmd;
      cmd.image_id = s.image_id;
      cmd.src_x = s.src.x;
      cmd.src_y = s.src.y;
      cmd.src_w = s.src.width;
      cmd.src_h = s.src.height;
      cmd.z_index = -1073741825;
      if (s.dst.width > 0) {
        cmd.columns = s.dst.width;
      }
      if (s.dst.height > 0) {
        cmd.rows = s.dst.height;
      }
      out += cmd.serialize();
    }
  }

  tty_write(out);
  std::fflush(tty_);
}

void NeovimFrontend::statusline(const std::string& /*left*/, const std::string& /*right*/) {
  // In Neovim mode, the plugin handles the statusline.
  // We send a state_changed notification so the plugin can update.
  state_dirty_ = true;
}

void NeovimFrontend::show_overlay(const std::string& /*title*/, const std::vector<std::string>& /*lines*/) {
  // No-op: plugin uses floating windows for overlays.
}

void NeovimFrontend::clear_overlay() {
  // No-op.
}

void NeovimFrontend::show_sidebar(const std::vector<std::string>& /*lines*/, int /*highlight_line*/, int /*width_cols*/, bool /*focused*/) {
  // No-op: no sidebar in Neovim mode (plugin uses Telescope).
}

void NeovimFrontend::show_link_hints(const std::vector<LinkHintDisplay>& hints) {
  if (hints.empty()) {
    return;
  }

  if (in_tmux_) {
    // Wrap delete + cursor positioning + text labels in DCS passthrough
    std::string raw;
    raw += kitty::delete_all_placements();
    for (const auto& hint : hints) {
      int term_row = tmux_pane_top_ + hint.row + offset_row_;
      int term_col = tmux_pane_left_ + hint.col + offset_col_;
      sgr::move_to(raw, term_row + 1, term_col + 1);
      raw += sgr::Bold;
      raw += colors_.link_hint_fg.sgr_fg();
      raw += colors_.link_hint_bg.sgr_bg();
      raw += hint.label;
      raw += sgr::Reset;
    }
    std::string out = tmux_dcs_wrap(raw);
    tty_write(out);
  }
  else {
    std::string out = kitty::delete_all_placements();
    for (const auto& hint : hints) {
      int row = hint.row + offset_row_;
      int col = hint.col + offset_col_;
      sgr::move_to(out, row + 1, col + 1);
      out += sgr::Bold;
      out += colors_.link_hint_fg.sgr_fg();
      out += colors_.link_hint_bg.sgr_bg();
      out += hint.label;
      out += sgr::Reset;
    }
    tty_write(out);
  }

  std::fflush(tty_);
}

void NeovimFrontend::write_raw(const char* data, size_t len) {
  tty_write(data, len);
  std::fflush(tty_);
}

std::optional<Command> NeovimFrontend::pop_command() {
  if (command_queue_.empty()) {
    return std::nullopt;
  }
  auto cmd = std::move(command_queue_.front());
  command_queue_.pop_front();
  return cmd;
}
