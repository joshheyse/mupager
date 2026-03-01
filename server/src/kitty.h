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
/// @param placement_id Optional placement ID for later replacement via place().
std::string encode(const Pixmap& pixmap, uint32_t image_id = 0, uint32_t placement_id = 0);

/// @brief Transmit a pixmap to the terminal without displaying it (a=t).
/// The image is stored by the terminal and can be placed later with place().
std::string transmit(const Pixmap& pixmap, uint32_t image_id);

/// @brief Place a previously transmitted image with source rect cropping (a=p).
/// Uses placement id 1, so repeated calls replace the previous placement.
std::string place(uint32_t image_id, int src_x, int src_y, int src_w, int src_h);

/// @brief Return an escape sequence that deletes the image with the given ID.
std::string delete_image(uint32_t image_id);

/// @brief Encode a pixmap for display inside tmux using DCS passthrough and Unicode placeholders.
/// @param pixmap The image to encode.
/// @param image_id Unique image ID (must be > 0).
/// @param cell_width_px Terminal cell width in pixels.
/// @param cell_height_px Terminal cell height in pixels.
/// @return Complete output string: DCS-wrapped transmit+display + Unicode placeholder text.
std::string encode_tmux(const Pixmap& pixmap, uint32_t image_id, int cell_width_px, int cell_height_px);

/// @brief Transmit a pixmap via tmux DCS passthrough without displaying it (a=t, U=1).
std::string transmit_tmux(const Pixmap& pixmap, uint32_t image_id, int cols, int rows);

/// @brief Generate Unicode placeholder text for a range of cell rows.
/// @param image_id The image ID encoded in the foreground color.
/// @param first_row First cell row index to display.
/// @param num_rows Number of cell rows to display.
/// @param num_cols Number of cell columns per row.
std::string placeholders(uint32_t image_id, int first_row, int num_rows, int num_cols);

/// @brief Return a DCS-wrapped escape sequence that deletes the image with the given ID.
std::string delete_image_tmux(uint32_t image_id);

} // namespace kitty
