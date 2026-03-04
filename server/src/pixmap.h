#pragma once

#include "color.h"
#include "geometry.h"

#include <mupdf/fitz.h>

#include <cstdint>
#include <memory>
#include <vector>

/// @brief Deleter for fz_pixmap unique_ptr.
struct PixmapDeleter {
  fz_context* ctx;
  void operator()(fz_pixmap* pix) const;
};

/// @brief RAII wrapper around a MuPDF pixmap (rendered page bitmap).
class Pixmap {
public:
  /// @brief Take ownership of a MuPDF pixmap.
  Pixmap(fz_context* ctx, fz_pixmap* pix);

  /// @brief Width in pixels.
  int width() const;

  /// @brief Height in pixels.
  int height() const;

  /// @brief Stride (bytes per row, may include padding).
  int stride() const;

  /// @brief Number of color components per pixel (e.g. 3 for RGB).
  int components() const;

  /// @brief Raw pixel data.
  unsigned char* samples();

  /// @copydoc samples()
  const unsigned char* samples() const;

  /// @brief Pack pixels into a contiguous buffer with stride padding removed.
  std::vector<unsigned char> pack_pixels() const;

  /// @brief Invert all pixel values in place (255 - value per channel).
  void invert();

  /// @brief Alpha-blend a colored rectangle onto the pixmap.
  /// @param rect Rectangle in pixel coordinates (clamped to bounds).
  /// @param color Blend color.
  /// @param alpha Blend factor (0 = transparent, 255 = opaque).
  void highlight_rect(PixelRect rect, Color color, uint8_t alpha);

  /// @brief Recolor the pixmap using Zathura-style interpolation.
  ///
  /// Maps black pixels to fg and white pixels to bg,
  /// with intermediate values interpolated by grayscale luminance.
  void recolor(Color fg, Color bg);

  /// @brief Encode pixmap as PNG data.
  std::vector<unsigned char> png_data() const;

private:
  std::unique_ptr<fz_pixmap, PixmapDeleter> pix_;
};
