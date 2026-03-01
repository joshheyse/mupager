#pragma once

#include <cstdint>
#include <string>

class Pixmap;

/// Kitty graphics protocol escape sequence encoding.
namespace kitty {

/// @brief Wrap APC escape sequences in DCS passthrough for tmux.
/// Each APC sequence is individually wrapped with doubled ESC bytes.
std::string wrap_tmux(const std::string& escape);

/// @brief Encode a pixmap as Kitty graphics protocol escape sequences for direct terminal display.
std::string encode(const Pixmap& pixmap, uint32_t image_id = 0);

/// @brief Return an escape sequence that deletes the image with the given ID.
std::string delete_image(uint32_t image_id);

/// @brief Encode a pixmap for display inside tmux using DCS passthrough and Unicode placeholders.
/// @param pixmap The image to encode.
/// @param image_id Unique image ID (must be > 0).
/// @param cell_width_px Terminal cell width in pixels.
/// @param cell_height_px Terminal cell height in pixels.
/// @return Complete output string: DCS-wrapped transmit+display + Unicode placeholder text.
std::string encode_tmux(const Pixmap& pixmap, uint32_t image_id, int cell_width_px, int cell_height_px);

/// @brief Return a DCS-wrapped escape sequence that deletes the image with the given ID.
std::string delete_image_tmux(uint32_t image_id);

} // namespace kitty
