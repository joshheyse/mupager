#pragma once

#include "color.hpp"
#include "color_scheme.hpp"
#include "document.hpp"
#include "frontend.hpp"
#include "geometry.hpp"
#include "graphics/pixmap.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

enum class Theme;
enum class AppMode;

/// @brief Parameters for rendering a page (MuPDF + theme).
struct RenderParams {
  float user_zoom;       ///< User zoom multiplier.
  float render_scale;    ///< effective_render_scale() result (0 = never).
  Theme effective_theme; ///< Resolved theme (dark/light/terminal).
  Color recolor_fg;      ///< Resolved fg for terminal theme.
  Color recolor_bg;      ///< Resolved bg for terminal theme.
  Color recolor_accent;  ///< Accent color for terminal theme.
  CellSize client_cell;  ///< Terminal cell size for grid computation.
  Color separator_color; ///< Color for the 1px separator line between pages.
  bool draw_separator;   ///< Whether to draw separators (continuous mode).
};

/// @brief Parameters for highlight overlays.
struct HighlightParams {
  const SearchResults* search_results; ///< Search results to highlight (may be nullptr).
  const ColorScheme* colors;           ///< Color scheme for highlight colors.
  AppMode app_mode;                    ///< Current app mode (for selection highlighting).
  PagePoint selection_anchor;          ///< Selection anchor point.
  PagePoint selection_extent;          ///< Selection extent point.
};

/// @brief A rendered page image with cache/display metadata.
///
/// Holds post-theme, pre-highlight base pixels plus the KGP upload state.
/// Can produce highlighted copies without re-rendering from MuPDF.
class Page {
public:
  /// @brief Render a page from the document and upload it.
  /// @return A fully constructed Page with image uploaded to the frontend.
  static Page render(
      const Document& doc,
      int page_num,
      const RenderParams& params,
      float base_zoom,
      const HighlightParams& highlights,
      Frontend& frontend
  );

  /// @brief Re-highlight from base pixels and re-upload (fast path).
  void refresh_highlights(const Document& doc, int page_num, const HighlightParams& params, Frontend& frontend);

  /// @brief Free the uploaded image.
  void free_image(Frontend& frontend);

  /// @brief KGP image handle.
  uint32_t image_id() const { return image_id_; }

  /// @brief Rendered pixel dimensions.
  PixelSize pixel_size() const { return {width_, height_}; }

  /// @brief Display grid dimensions in cells.
  CellSize cell_grid() const { return cell_grid_; }

  /// @brief Render scale this was rendered at (0 = exact zoom, no viewporting).
  float render_scale() const { return render_scale_; }

  /// @brief Actual MuPDF zoom (for cache invalidation in NEVER mode).
  float render_zoom() const { return render_zoom_; }

  /// @brief Uncompressed pixmap size in bytes.
  size_t memory_bytes() const { return base_pixels_.size(); }

private:
  Page(std::vector<unsigned char> pixels, int w, int h, int comp, float zoom, float scale, uint32_t id, CellSize grid);

  /// @brief Produce a Pixmap with highlights applied (from base pixels).
  Pixmap highlight(const Document& doc, int page_num, const HighlightParams& params) const;

  /// @brief Reconstruct a Pixmap from the stored base pixels.
  Pixmap to_pixmap(fz_context* ctx) const;

  static void highlight_search(Pixmap& pixmap, int page_num, float render_zoom, const HighlightParams& params);
  static void highlight_selection(Pixmap& pixmap, const Document& doc, int page_num, float render_zoom, const HighlightParams& params);

  std::vector<unsigned char> base_pixels_;
  int width_;
  int height_;
  int comp_;
  float render_zoom_;
  float render_scale_;
  uint32_t image_id_;
  CellSize cell_grid_;
};
