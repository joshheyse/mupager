#include "osc_query.hpp"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>

/// @brief Parse an OSC color response component "RRRR/GGGG/BBBB" (16-bit hex per channel).
static std::optional<Color> parse_osc_color_response(const std::string& payload) {
  // Response format: \x1b]1x;rgb:RRRR/GGGG/BBBB\x1b\\ or \x1b]1x;rgb:RRRR/GGGG/BBBB\x07
  // Find "rgb:" prefix
  auto rgb_pos = payload.find("rgb:");
  if (rgb_pos == std::string::npos) {
    return std::nullopt;
  }

  std::string rgb_str = payload.substr(rgb_pos + 4);
  // Strip trailing ST (\x1b\\ or \x07)
  if (!rgb_str.empty() && rgb_str.back() == '\\') {
    rgb_str.pop_back();
    if (!rgb_str.empty() && rgb_str.back() == '\x1b') {
      rgb_str.pop_back();
    }
  }
  if (!rgb_str.empty() && rgb_str.back() == '\x07') {
    rgb_str.pop_back();
  }

  // Parse RRRR/GGGG/BBBB
  size_t slash1 = rgb_str.find('/');
  if (slash1 == std::string::npos) {
    return std::nullopt;
  }
  size_t slash2 = rgb_str.find('/', slash1 + 1);
  if (slash2 == std::string::npos) {
    return std::nullopt;
  }

  auto parse_component = [](const std::string& s) -> std::optional<uint8_t> {
    unsigned long val = 0;
    try {
      val = std::stoul(s, nullptr, 16);
    }
    catch (...) {
      return std::nullopt;
    }
    // 16-bit value: take high byte. If only 2 hex digits (8-bit), use as-is.
    if (s.size() <= 2) {
      return static_cast<uint8_t>(val);
    }
    return static_cast<uint8_t>(val >> 8);
  };

  auto rv = parse_component(rgb_str.substr(0, slash1));
  auto gv = parse_component(rgb_str.substr(slash1 + 1, slash2 - slash1 - 1));
  auto bv = parse_component(rgb_str.substr(slash2 + 1));

  if (!rv || !gv || !bv) {
    return std::nullopt;
  }

  return Color::rgb(*rv, *gv, *bv);
}

std::optional<std::pair<Color, Color>> query_terminal_colors(int timeout_ms) {
  int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
  if (fd < 0) {
    return std::nullopt;
  }

  // Save and set raw mode
  struct termios old_tio{};
  struct termios new_tio{};
  if (tcgetattr(fd, &old_tio) < 0) {
    close(fd);
    return std::nullopt;
  }
  new_tio = old_tio;
  new_tio.c_lflag &= ~(ICANON | ECHO);
  new_tio.c_cc[VMIN] = 0;
  new_tio.c_cc[VTIME] = 0;
  tcsetattr(fd, TCSANOW, &new_tio);

  // Send OSC 10 (foreground) and OSC 11 (background) queries
  bool in_tmux = std::getenv("TMUX") != nullptr;
  std::string query;
  if (in_tmux) {
    // DCS passthrough for tmux
    query = "\x1bPtmux;\x1b\x1b]10;?\x07\x1b\\";
    query += "\x1bPtmux;\x1b\x1b]11;?\x07\x1b\\";
  }
  else {
    query = "\x1b]10;?\x1b\\";
    query += "\x1b]11;?\x1b\\";
  }
  write(fd, query.data(), query.size());

  // Read response
  std::string buf;
  buf.reserve(256);
  struct pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLIN;

  int remaining_ms = timeout_ms;
  while (remaining_ms > 0) {
    int ret = poll(&pfd, 1, remaining_ms);
    if (ret <= 0) {
      break;
    }
    char tmp[128];
    ssize_t n = read(fd, tmp, sizeof(tmp));
    if (n <= 0) {
      break;
    }
    buf.append(tmp, n);
    // Check if we have two complete responses (look for two STs)
    int st_count = 0;
    for (size_t i = 0; i < buf.size(); ++i) {
      if (buf[i] == '\x07') {
        ++st_count;
      }
      else if (buf[i] == '\\' && i > 0 && buf[i - 1] == '\x1b') {
        ++st_count;
      }
    }
    if (st_count >= 2) {
      break;
    }
    remaining_ms -= 10; // Approximate: poll doesn't tell us elapsed time
  }

  // Restore terminal
  tcsetattr(fd, TCSANOW, &old_tio);
  close(fd);

  if (buf.empty()) {
    return std::nullopt;
  }

  // Split into individual responses at ESC ] boundaries
  std::optional<Color> fg;
  std::optional<Color> bg;

  // Find OSC 10 response (contains "10;")
  auto pos10 = buf.find("10;");
  if (pos10 != std::string::npos) {
    fg = parse_osc_color_response(buf.substr(pos10));
  }

  // Find OSC 11 response (contains "11;")
  auto pos11 = buf.find("11;");
  if (pos11 != std::string::npos) {
    bg = parse_osc_color_response(buf.substr(pos11));
  }

  if (fg && bg) {
    return std::make_pair(*fg, *bg);
  }

  return std::nullopt;
}
