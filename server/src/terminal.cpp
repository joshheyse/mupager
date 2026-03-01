#include "terminal.h"

#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

namespace terminal {

CellSize cell_size() {
  struct winsize ws {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
    throw std::runtime_error("TIOCGWINSZ ioctl failed");
  }
  if (ws.ws_col == 0 || ws.ws_row == 0 || ws.ws_xpixel == 0 || ws.ws_ypixel == 0) {
    throw std::runtime_error("terminal reported zero dimensions");
  }
  return {static_cast<int>(ws.ws_xpixel / ws.ws_col), static_cast<int>(ws.ws_ypixel / ws.ws_row)};
}

} // namespace terminal
