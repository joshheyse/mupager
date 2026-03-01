#pragma once

#include "input_event.h"
#include "pixmap.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/// @brief Describes how to display a slice of a page image on screen.
struct PageSlice {
  uint32_t image_id;              ///< Kitty image ID for this page.
  int src_x, src_y, src_w, src_h; ///< Source rect in page image (pixels).
  int dst_row, dst_rows;          ///< Screen position and height (cell rows).
  int img_cols, img_rows;         ///< Image grid dimensions (for tmux placeholders).
};

/// @brief Abstract interface for the display frontend.
class Frontend {
public:
  virtual ~Frontend() = default;

  /// @brief Poll for an input event with a timeout.
  /// @param timeout_ms Timeout in milliseconds (-1 for blocking).
  /// @return An InputEvent if one was received, std::nullopt on timeout.
  virtual std::optional<InputEvent> poll_input(int timeout_ms) = 0;

  /// @brief Clear the screen and release all images.
  virtual void clear() = 0;

  /// @brief Return the total pixel dimensions of the display region.
  /// @return {height_px, width_px}.
  virtual std::pair<unsigned, unsigned> pixel_size() = 0;

  /// @brief Return the pixel dimensions of a single cell.
  /// @return {cell_height_px, cell_width_px}.
  virtual std::pair<unsigned, unsigned> cell_size() = 0;

  /// @brief Upload a page image to the terminal without displaying it.
  /// @param pixmap The rendered page pixmap to upload.
  /// @param cols Image grid columns (for tmux placeholders).
  /// @param rows Image grid rows (for tmux placeholders).
  /// @return Assigned Kitty image ID.
  virtual uint32_t upload_image(const Pixmap& pixmap, int cols, int rows) = 0;

  /// @brief Free a previously uploaded image.
  /// @param image_id The image ID to delete.
  virtual void free_image(uint32_t image_id) = 0;

  /// @brief Display page slices on screen.
  /// @param slices Vector of PageSlice describing each visible page region.
  virtual void show_pages(const std::vector<PageSlice>& slices) = 0;

  /// @brief Update the status line. Stub for Phase 4+.
  virtual void statusline(const std::string& text) = 0;
};
