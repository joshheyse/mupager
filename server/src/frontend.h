#pragma once

#include "color_scheme.h"
#include "geometry.h"
#include "input_event.h"
#include "pixmap.h"
#include "rpc_command.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// @brief A positioned label for link hint overlay.
struct LinkHintDisplay {
  int col;           ///< Screen column (0-based).
  int row;           ///< Screen row (0-based).
  std::string label; ///< Hint label text (e.g. "a", "sd").
};

/// @brief Describes how to display a slice of a page image on screen.
struct PageSlice {
  uint32_t image_id;     ///< Kitty image ID for this page.
  PixelRect src;         ///< Source rect in page image (pixels).
  CellRect dst;          ///< Destination rect on screen (cells). Width = visible columns.
  CellSize img_grid;     ///< Image grid dimensions (for tmux placeholders).
  PixelSize img_px_size; ///< Full cached image pixel dimensions (for cell-column mapping).
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

  /// @brief Query terminal state (pixel size, cell size, grid dimensions).
  virtual ClientInfo client_info() = 0;

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

  /// @brief Update the status line with left/right aligned sections.
  /// @param left Left-aligned text (command/search input).
  /// @param right Right-aligned text (mode, theme, page info).
  virtual void statusline(const std::string& left, const std::string& right) = 0;

  /// @brief Show a centered overlay box with a title in the top border and body lines.
  /// @param title Title text displayed in the top border row.
  /// @param lines Body lines to display inside the overlay.
  virtual void show_overlay(const std::string& title, const std::vector<std::string>& lines) = 0;

  /// @brief Clear the overlay (no-op if repainting handles it).
  virtual void clear_overlay() = 0;

  /// @brief Show a sidebar panel on the left edge of the screen.
  /// @param lines Lines to display in the sidebar.
  /// @param highlight_line Index of the highlighted line (-1 for none).
  /// @param width_cols Width of the sidebar in columns.
  /// @param focused Whether the sidebar has input focus (affects highlight style).
  virtual void show_sidebar(const std::vector<std::string>& lines, int highlight_line, int width_cols, bool focused) = 0;

  /// @brief Show link hint labels at screen positions.
  /// @param hints Vector of positioned labels.
  virtual void show_link_hints(const std::vector<LinkHintDisplay>& hints) = 0;

  /// @brief Write raw bytes to the terminal (e.g. for OSC 52 clipboard).
  /// @param data Raw byte string to write.
  /// @param len Number of bytes to write.
  virtual void write_raw(const char* data, size_t len) = 0;

  /// @brief Whether the frontend supports Kitty image viewporting (source-rect cropping on place).
  /// Tmux unicode placeholders cannot viewport, so this returns false in tmux mode.
  virtual bool supports_image_viewporting() const = 0;

  /// @brief Apply a color scheme to the frontend.
  /// Default no-op; frontends override to store and use themed colors.
  virtual void set_color_scheme(const ColorScheme& scheme) {
    (void)scheme;
  }

  /// @brief Pop a queued RPC command (Neovim frontend only).
  /// @return The next command, or nullopt if the queue is empty.
  virtual std::optional<RpcCommand> pop_command() {
    return std::nullopt;
  }
};
