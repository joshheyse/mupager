#include "frontend.hpp"

#include "graphics/kitty.hpp"

#include <cstdlib>

Frontend::Frontend(uint32_t initial_image_id)
    : next_image_id_(initial_image_id) {
  in_tmux_ = std::getenv("TMUX") != nullptr;
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

bool Frontend::supports_image_viewporting() const {
  return !in_tmux_;
}

void Frontend::set_color_scheme(const ColorScheme& scheme) {
  colors_ = scheme;
}
