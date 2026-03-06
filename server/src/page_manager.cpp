#include "page_manager.hpp"

#include "app.hpp"
#include "document.hpp"
#include "frontend.hpp"
#include "page.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

PageManager::PageManager(size_t max_cache_bytes)
    : max_cache_bytes_(max_cache_bytes) {}

void PageManager::ensure_uploaded(
    int first,
    int last,
    const Document& doc,
    Frontend& frontend,
    const std::vector<PageLayout>& layout,
    const RenderParams& render,
    const HighlightParams& highlights
) {
  auto client = frontend.client_info();
  if (!client.is_valid()) {
    return;
  }

  for (int i = first; i <= last; ++i) {
    float base_zoom = layout[i].zoom;
    float render_zoom = (render.render_scale == 0.0f) ? base_zoom * render.user_zoom : base_zoom * render.render_scale;

    auto it = cache_.find(i);
    if (it != cache_.end()) {
      if (render.render_scale == 0.0f) {
        if (it->second.render_zoom() == render_zoom) {
          continue;
        }
        it->second.free_image(frontend);
        cache_.erase(it);
      }
      else {
        if (it->second.render_scale() >= render.render_scale && it->second.render_zoom() == render_zoom) {
          continue;
        }
        it->second.free_image(frontend);
        cache_.erase(it);
      }
    }

    cache_.insert_or_assign(i, Page::render(doc, i, render, base_zoom, highlights, frontend));
  }
}

void PageManager::evict_distant(int keep_first, int keep_last, int layout_size, Frontend& frontend) {
  int keep_lo = std::max(0, keep_first - 2);
  int keep_hi = std::min(layout_size - 1, keep_last + 2);

  std::vector<int> to_evict;
  for (auto& [page, cached] : cache_) {
    if (page < keep_lo || page > keep_hi) {
      to_evict.push_back(page);
    }
  }
  for (int page : to_evict) {
    auto it = cache_.find(page);
    it->second.free_image(frontend);
    cache_.erase(it);
    spdlog::debug("evicted page {}", page);
  }

  if (max_cache_bytes_ == 0) {
    return;
  }
  size_t total = 0;
  for (const auto& [page, cached] : cache_) {
    total += cached.memory_bytes();
  }
  if (total <= max_cache_bytes_) {
    return;
  }

  int mid = (keep_first + keep_last) / 2;
  std::vector<int> pages_by_distance;
  pages_by_distance.reserve(cache_.size());
  for (const auto& [page, cached] : cache_) {
    pages_by_distance.push_back(page);
  }
  std::sort(pages_by_distance.begin(), pages_by_distance.end(), [mid](int a, int b) { return std::abs(a - mid) > std::abs(b - mid); });

  for (int page : pages_by_distance) {
    if (total <= max_cache_bytes_) {
      break;
    }
    if (page >= keep_first && page <= keep_last) {
      continue;
    }
    auto it = cache_.find(page);
    total -= it->second.memory_bytes();
    it->second.free_image(frontend);
    cache_.erase(it);
    spdlog::debug("evicted page {} (cache over budget)", page);
  }
}

void PageManager::refresh_highlights(int first, int last, const Document& doc, Frontend& frontend, const HighlightParams& highlights) {
  first = std::max(first, 0);

  auto client = frontend.client_info();
  if (!client.is_valid()) {
    return;
  }

  for (int p = first; p <= last; ++p) {
    auto it = cache_.find(p);
    if (it == cache_.end()) {
      continue;
    }
    it->second.refresh_highlights(doc, p, highlights, frontend);
  }
}

bool PageManager::pre_upload_one(
    int viewport_first,
    int viewport_last,
    int num_pages,
    const Document& doc,
    Frontend& frontend,
    const std::vector<PageLayout>& layout,
    const RenderParams& render,
    const HighlightParams& highlights,
    const SearchResults& search_results,
    int search_current
) {
  std::vector<int> candidates = {
      viewport_first - 1,
      viewport_last + 1,
      viewport_first - 2,
      viewport_last + 2,
  };

  if (!search_results.empty() && search_current >= 0) {
    int n = static_cast<int>(search_results.size());
    int next_idx = (search_current + 1) % n;
    int prev_idx = (search_current - 1 + n) % n;
    candidates.push_back(search_results[next_idx].page);
    candidates.push_back(search_results[prev_idx].page);
  }

  for (int page : candidates) {
    if (page < 0 || page >= num_pages) {
      continue;
    }
    if (cache_.count(page) > 0) {
      continue;
    }
    ensure_uploaded(page, page, doc, frontend, layout, render, highlights);
    return true;
  }
  return false;
}

void PageManager::clear(Frontend& frontend) {
  for (auto& [page, cached] : cache_) {
    cached.free_image(frontend);
  }
  cache_.clear();
}

const Page* PageManager::get(int page) const {
  auto it = cache_.find(page);
  if (it == cache_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool PageManager::contains(int page) const {
  return cache_.count(page) > 0;
}

std::pair<std::string, size_t> PageManager::stats() const {
  std::vector<int> pages;
  pages.reserve(cache_.size());
  size_t total = 0;
  for (const auto& [page, cached] : cache_) {
    pages.push_back(page + 1);
    total += cached.memory_bytes();
  }
  std::sort(pages.begin(), pages.end());

  std::string result;
  for (size_t i = 0; i < pages.size();) {
    size_t j = i;
    while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1) {
      ++j;
    }
    if (!result.empty()) {
      result += ',';
    }
    if (j == i) {
      result += std::to_string(pages[i]);
    }
    else {
      result += std::to_string(pages[i]) + '-' + std::to_string(pages[j]);
    }
    i = j + 1;
  }
  return {result, total};
}
