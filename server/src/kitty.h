#pragma once

#include <cstdint>
#include <string>

class Pixmap;

/// Kitty graphics protocol escape sequence encoding.
namespace kitty {

/// Encode a pixmap as Kitty graphics protocol escape sequences for terminal display.
std::string encode(const Pixmap& pixmap, uint32_t image_id = 0);

/// Return an escape sequence that deletes the image with the given ID.
std::string delete_image(uint32_t image_id);

} // namespace kitty
