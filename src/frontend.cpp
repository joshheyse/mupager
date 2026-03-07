#include "frontend.hpp"

#include "color_scheme.hpp"
#include "graphics/kitty.hpp"
#include "util/base64.hpp"

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>

Frontend::Frontend(uint32_t initial_image_id)
    : next_image_id_(initial_image_id) {
  in_tmux_ = std::getenv("TMUX") != nullptr;

  // Seed image IDs from PID so concurrent instances don't collide
  // in Kitty's shared image store. Upper 16 bits = PID, lower 16 bits
  // count up, giving each process ~65k unique IDs.
  next_image_id_ = (static_cast<uint32_t>(getpid()) << 16) | 1u;
}

std::string Frontend::build_delete_sequence(uint32_t image_id) const {
  std::string seq = kitty::delete_image(image_id);
  if (in_tmux_) {
    seq = kitty::wrap_tmux(seq);
  }
  return seq;
}

std::string Frontend::build_image_cleanup_sequence() const {
  std::string out;
  for (uint32_t id : uploaded_ids_) {
    out += build_delete_sequence(id);
  }
  return out;
}

void Frontend::free_image(uint32_t image_id) {
  if (uploaded_ids_.count(image_id) == 0) {
    return;
  }
  std::string seq = build_delete_sequence(image_id);
  write_raw(seq.data(), seq.size());
  uploaded_ids_.erase(image_id);
}

void Frontend::update_image_grid(uint32_t /*image_id*/, int /*cols*/, int /*rows*/) {}

void Frontend::set_color_scheme(const ColorScheme& scheme) {
  colors_ = scheme;
}

void Frontend::copy_to_clipboard(const std::string& text) {
  if (text.empty()) {
    return;
  }
  std::string osc52 = "\x1b]52;c;" + base64::encode(text) + "\x07";
  write_raw(osc52.data(), osc52.size());
}
