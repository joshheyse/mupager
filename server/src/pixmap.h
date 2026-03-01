#pragma once

#include <mupdf/fitz.h>

#include <memory>

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

private:
  std::unique_ptr<fz_pixmap, PixmapDeleter> pix_;
};
