#pragma once

#include "outline.h"
#include "pixmap.h"

#include <mupdf/fitz.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

/// @brief A point on a specific page in page coordinate space.
struct PagePoint {
  int page;   ///< Zero-based page index.
  float x, y; ///< Position in page points.
};

/// @brief A single search hit: page index + bounding box in page points.
struct SearchHit {
  int page;         ///< Zero-based page index.
  float x, y, w, h; ///< Bounding rect in page coordinate space (points at zoom=1).
};

/// @brief All search results for a query.
using SearchResults = std::vector<SearchHit>;

/// @brief A hyperlink extracted from a PDF page.
struct PageLink {
  int page;             ///< Source page (zero-based).
  float x, y, w, h;     ///< Bounding box in page point coordinates.
  std::string uri;      ///< Link destination URI.
  int dest_page;        ///< Resolved destination page (-1 for external).
  float dest_x, dest_y; ///< Resolved anchor position within dest page (points).
};

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

  /// @brief Load the document outline (table of contents).
  /// @return Flattened outline entries, empty if the document has no outline.
  Outline load_outline() const;

  /// @brief Render a sub-region of a page to an RGB pixmap.
  /// @param page_num Zero-based page index.
  /// @param zoom Scale factor (1.0 = 72 DPI).
  /// @param x_offset Horizontal pixel offset into the zoomed page.
  /// @param y_offset Vertical pixel offset into the zoomed page.
  /// @param viewport_w Viewport width in pixels.
  /// @param viewport_h Viewport height in pixels.
  Pixmap render_page(int page_num, float zoom, int x_offset, int y_offset, int viewport_w, int viewport_h) const;

  /// @brief Search all pages for text, returning bounding boxes.
  /// @param needle Text to search for.
  /// @return All matching bounding boxes across the document.
  SearchResults search(const std::string& needle) const;

  /// @brief Reload the document from disk.
  /// @param path Path to the document file.
  /// @throws std::runtime_error if the file cannot be opened (original document is preserved).
  void reload(const std::string& path);

  /// @brief Load hyperlinks from a page.
  /// @param page_num Zero-based page index.
  /// @return Vector of PageLink for the page.
  std::vector<PageLink> load_links(int page_num) const;

  /// @brief Copy text between two page points (character-mode selection).
  /// @param a Anchor point.
  /// @param b Extent point.
  /// @return Extracted text, pages joined by newlines.
  std::string copy_text(const PagePoint& a, const PagePoint& b) const;

  /// @brief Get highlight quads for a character-mode selection on a page.
  /// @param page_num Zero-based page index.
  /// @param a Anchor point.
  /// @param b Extent point.
  /// @return Bounding boxes for highlighted characters.
  std::vector<SearchHit> selection_quads(int page_num, const PagePoint& a, const PagePoint& b) const;

  /// @brief Copy text within a rectangular region on a page (block-mode selection).
  /// @param page_num Zero-based page index.
  /// @param x0 Left edge in page points.
  /// @param y0 Top edge in page points.
  /// @param x1 Right edge in page points.
  /// @param y1 Bottom edge in page points.
  /// @return Extracted text from the rectangle.
  std::string copy_rect_text(int page_num, float x0, float y0, float x1, float y1) const;

  /// @brief Get highlight quads for a rectangular selection on a page.
  /// @param page_num Zero-based page index.
  /// @param x0 Left edge in page points.
  /// @param y0 Top edge in page points.
  /// @param x1 Right edge in page points.
  /// @param y1 Bottom edge in page points.
  /// @return Bounding boxes for highlighted characters.
  std::vector<SearchHit> rect_selection_quads(int page_num, float x0, float y0, float x1, float y1) const;

  /// @brief Find the next word boundary after a given point.
  /// @param from Starting point in page coordinates.
  /// @return The page point at the next word boundary.
  PagePoint next_word_boundary(const PagePoint& from) const;

  /// @brief Find the previous word boundary before a given point.
  /// @param from Starting point in page coordinates.
  /// @return The page point at the previous word boundary.
  PagePoint prev_word_boundary(const PagePoint& from) const;

  /// @brief Find the end of the current or next word from a given point.
  /// @param from Starting point in page coordinates.
  /// @return The page point at the last character of the word.
  PagePoint end_of_word_boundary(const PagePoint& from) const;

  /// @brief Find the first character on the stext line containing a point.
  /// @param from Starting point in page coordinates.
  /// @return The page point at the start of the line.
  PagePoint line_start(const PagePoint& from) const;

  /// @brief Find the last character on the stext line containing a point.
  /// @param from Starting point in page coordinates.
  /// @return The page point at the end of the line.
  PagePoint line_end(const PagePoint& from) const;

  /// @brief Find the first non-whitespace character on the stext line containing a point.
  /// @param from Starting point in page coordinates.
  /// @return The page point at the first non-space character.
  PagePoint first_non_space(const PagePoint& from) const;

  /// @brief Return the underlying MuPDF context.
  fz_context* ctx() const;

  /// @brief Return the underlying MuPDF document handle.
  fz_document* raw() const;

private:
  std::unique_ptr<fz_context, ContextDeleter> ctx_;
  std::unique_ptr<fz_document, DocumentDeleter> doc_;
};
