#pragma once

#include "document.hpp"
#include "frontend.hpp"
#include "page.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct PageLayout;

/// @brief Manages rendered page cache, upload/eviction lifecycle.
class PageManager {
public:
  explicit PageManager(size_t max_cache_bytes);

  /// @brief Ensure pages [first, last] are rendered and uploaded.
  /// Renders cache misses via Page::render(), uploads via frontend.
  void ensure_uploaded(
      int first,
      int last,
      const Document& doc,
      Frontend& frontend,
      const std::vector<PageLayout>& layout,
      const RenderParams& render,
      const HighlightParams& highlights
  );

  /// @brief Evict cached pages when over budget, furthest from viewport first.
  void evict_over_budget(int keep_first, int keep_last, Frontend& frontend);

  /// @brief Re-highlight affected pages from base pixels (fast path).
  void refresh_highlights(int first, int last, const Document& doc, Frontend& frontend, const HighlightParams& highlights);

  /// @brief Pre-render one page to fill the cache (called during idle).
  /// Prioritizes pages near the viewport, then search results.
  /// @return true if a page was uploaded, false if cache is full or all pages cached.
  bool pre_render_one(
      int viewport_first,
      int viewport_last,
      int num_pages,
      const Document& doc,
      Frontend& frontend,
      const std::vector<PageLayout>& layout,
      const RenderParams& render,
      const HighlightParams& highlights
  );

  /// @brief Free all cached images.
  void clear(Frontend& frontend);

  /// @brief Get cache entry for a page (nullptr if not cached).
  const Page* get(int page) const;

  /// @brief Check if a page is cached.
  bool contains(int page) const;

  /// @brief Format cache stats for statusline debug display.
  std::pair<std::string, size_t> stats() const;

private:
  std::unordered_map<int, Page> cache_;
  size_t max_cache_bytes_;
};
