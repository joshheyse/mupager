#include "terminal_frontend.h"

#include <notcurses/notcurses.h>
#include <spdlog/spdlog.h>

#include <stdexcept>

TerminalFrontend::TerminalFrontend() {
  notcurses_options opts{};
  opts.flags = NCOPTION_SUPPRESS_BANNERS;
  nc_ = notcurses_core_init(&opts, nullptr);
  if (!nc_) {
    throw std::runtime_error("notcurses_core_init failed");
  }
  std_plane_ = notcurses_stdplane(nc_);
}

TerminalFrontend::~TerminalFrontend() {
  if (nc_) {
    notcurses_stop(nc_);
  }
}

std::optional<InputEvent> TerminalFrontend::poll_input(int timeout_ms) {
  struct timespec ts {};
  if (timeout_ms >= 0) {
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
  }
  ncinput ni{};
  uint32_t id = notcurses_get(nc_, timeout_ms < 0 ? nullptr : &ts, &ni);
  if (id == 0 || id == static_cast<uint32_t>(-1)) {
    return std::nullopt;
  }
  static constexpr EventType TYPE_MAP[] = {EventType::UNKNOWN, EventType::PRESS, EventType::REPEAT, EventType::RELEASE};
  auto type = static_cast<int>(ni.evtype) < 4 ? TYPE_MAP[static_cast<int>(ni.evtype)] : EventType::UNKNOWN;
  return InputEvent{ni.id, ni.modifiers, type};
}

void TerminalFrontend::clear() {
  ncplane_erase(std_plane_);
  notcurses_render(nc_);
}

std::pair<unsigned, unsigned> TerminalFrontend::pixel_size() {
  unsigned pxy = 0;
  unsigned pxx = 0;
  ncplane_pixel_geom(std_plane_, &pxy, &pxx, nullptr, nullptr, nullptr, nullptr);
  return {pxy, pxx};
}

std::pair<unsigned, unsigned> TerminalFrontend::cell_size() {
  unsigned celldimy = 0;
  unsigned celldimx = 0;
  ncplane_pixel_geom(std_plane_, nullptr, nullptr, &celldimy, &celldimx, nullptr, nullptr);
  return {celldimy, celldimx};
}

void TerminalFrontend::display() {}

void TerminalFrontend::statusline(const std::string& /*text*/) {}
