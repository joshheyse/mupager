#pragma once

#include "pixmap.h"

#include <mupdf/fitz.h>

#include <memory>
#include <string>
#include <utility>

/// @brief Deleter for fz_context unique_ptr.
struct ContextDeleter {
  void operator()(fz_context* ctx) const;
};

/// @brief Deleter for fz_document unique_ptr.
struct DocumentDeleter {
  fz_context* ctx;
  void operator()(fz_document* doc) const;
};

/// @brief RAII wrapper around a MuPDF document.
class Document {
public:
  /// @brief Open a document from a file path.
  /// @param path Path to the document file.
  /// @throws std::runtime_error if the file cannot be opened.
  explicit Document(const std::string& path);

  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;

  /// @brief Return the total number of pages.
  int page_count() const;

  /// @brief Return the page dimensions in points at zoom=1.
  /// @param page_num Zero-based page index.
  /// @return {width, height}.
  std::pair<float, float> page_size(int page_num) const;

  /// @brief Render a page to an RGB pixmap.
  /// @param page_num Zero-based page index.
  /// @param zoom Scale factor (1.0 = 72 DPI).
  /// @throws std::runtime_error if page_num is out of range.
  Pixmap render_page(int page_num, float zoom) const;

  /// @brief Render a sub-region of a page to an RGB pixmap.
  /// @param page_num Zero-based page index.
  /// @param zoom Scale factor (1.0 = 72 DPI).
  /// @param x_offset Horizontal pixel offset into the zoomed page.
  /// @param y_offset Vertical pixel offset into the zoomed page.
  /// @param viewport_w Viewport width in pixels.
  /// @param viewport_h Viewport height in pixels.
  Pixmap render_page(int page_num, float zoom, int x_offset, int y_offset, int viewport_w, int viewport_h) const;

  /// @brief Return the underlying MuPDF context.
  fz_context* ctx() const;

  /// @brief Return the underlying MuPDF document handle.
  fz_document* raw() const;

private:
  std::unique_ptr<fz_context, ContextDeleter> ctx_;
  std::unique_ptr<fz_document, DocumentDeleter> doc_;
};
