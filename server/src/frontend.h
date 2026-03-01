#pragma once

#include "input_event.h"
#include "pixmap.h"

#include <optional>
#include <string>
#include <utility>

/// @brief Abstract interface for the display frontend.
class Frontend {
public:
  virtual ~Frontend() = default;

  /// @brief Poll for an input event with a timeout.
  /// @param timeout_ms Timeout in milliseconds (-1 for blocking).
  /// @return An InputEvent if one was received, std::nullopt on timeout.
  virtual std::optional<InputEvent> poll_input(int timeout_ms) = 0;

  /// @brief Clear the screen and render.
  virtual void clear() = 0;

  /// @brief Return the total pixel dimensions of the display region.
  /// @return {height_px, width_px}.
  virtual std::pair<unsigned, unsigned> pixel_size() = 0;

  /// @brief Return the pixel dimensions of a single cell.
  /// @return {cell_height_px, cell_width_px}.
  virtual std::pair<unsigned, unsigned> cell_size() = 0;

  /// @brief Display a rendered page image.
  /// @param pixmap The rendered page pixmap to display.
  virtual void display(const Pixmap& pixmap) = 0;

  /// @brief Update the status line. Stub for Phase 4+.
  virtual void statusline(const std::string& text) = 0;
};
