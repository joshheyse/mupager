#include "app.h"

#include "base64.h"
#include "sgr.h"
#include "stopwatch.h"
#include "terminal_input.h"

#include <spdlog/spdlog.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <string>

std::optional<ViewMode> parse_view_mode(const std::string& s) {
  if (s == "continuous") {
    return ViewMode::CONTINUOUS;
  }
  if (s == "page" || s == "page-width") {
    return ViewMode::PAGE_WIDTH;
  }
  if (s == "page-height") {
    return ViewMode::PAGE_HEIGHT;
  }
  if (s == "side-by-side") {
    return ViewMode::SIDE_BY_SIDE;
  }
  return std::nullopt;
}

const char* to_label(Theme t, Theme effective) {
  switch (t) {
    case Theme::LIGHT:
      return "Light";
    case Theme::DARK:
      return "Dark";
    case Theme::AUTO:
      return (effective == Theme::DARK) ? "Auto/Dark" : "Auto/Light";
    case Theme::TERMINAL:
      return "Terminal";
  }
  return "";
}

std::optional<Theme> parse_theme(const std::string& s) {
  if (s == "dark") {
    return Theme::DARK;
  }
  if (s == "light") {
    return Theme::LIGHT;
  }
  if (s == "auto") {
    return Theme::AUTO;
  }
  if (s == "terminal") {
    return Theme::TERMINAL;
  }
  return std::nullopt;
}

App::App(std::unique_ptr<Frontend> frontend, const Args& args, std::optional<Color> detected_fg, std::optional<Color> detected_bg)
    : frontend_(std::move(frontend))
    , doc_(args.file)
    , colors_(args.colors)
    , detected_terminal_fg_(detected_fg)
    , detected_terminal_bg_(detected_bg)
    , show_stats_(args.show_stats)
    , scroll_lines_(args.scroll_lines)
    , render_scale_setting_(args.render_scale)
    , file_path_(args.file) {
  if (auto vm = parse_view_mode(args.view_mode)) {
    view_mode_ = *vm;
  }
  if (auto th = parse_theme(args.theme)) {
    theme_ = *th;
  }
  spdlog::info("opened document: {} pages, mode: {}, theme: {}", doc_.page_count(), args.view_mode, args.theme);
}

void App::build_layout() {
  auto client = frontend_->client_info();
  layout_size_ = client.pixel;
  int num_pages = doc_.page_count();
  auto vp = client.viewport_pixel();
  int vh = vp.height;
  int vw = vp.width;

  int sidebar_cols = sidebar_effective_width();
  if (sidebar_cols > 0 && client.cell.width > 0) {
    vw -= sidebar_cols * client.cell.width;
  }

  spdlog::debug("build_layout: pixel={} vp={} cell={} pages={} zoom={:.2f}", client.pixel, vp, client.cell, num_pages, user_zoom_);
  layout_.resize(num_pages);

  if (view_mode_ == ViewMode::PAGE_HEIGHT) {
    for (int i = 0; i < num_pages; ++i) {
      auto [page_w, page_h] = doc_.page_size(i);
      float zoom = std::min(static_cast<float>(vw) / page_w, static_cast<float>(vh) / page_h);
      int rendered_w = static_cast<int>(page_w * zoom);
      int slot_h = std::max(vh, static_cast<int>(page_h * zoom * user_zoom_));
      layout_[i] = {{0, i * slot_h, rendered_w, slot_h}, zoom};
    }
  }
  else if (view_mode_ == ViewMode::SIDE_BY_SIDE) {
    int half_w = vw / 2;
    int y = 0;
    for (int i = 0; i < num_pages; ++i) {
      auto [page_w, page_h] = doc_.page_size(i);
      bool is_last_odd = (i == num_pages - 1) && (i % 2 == 0);
      int fit_w = is_last_odd ? vw : half_w;
      float zoom = std::min(static_cast<float>(fit_w) / page_w, static_cast<float>(vh) / page_h);
      int rendered_w = static_cast<int>(page_w * zoom);
      int slot_h = std::max(vh, static_cast<int>(page_h * zoom * user_zoom_));
      if (i % 2 == 0) {
        layout_[i] = {{0, y, rendered_w, slot_h}, zoom};
      }
      else {
        layout_[i] = {{0, layout_[i - 1].rect.y, rendered_w, slot_h}, zoom};
        y += slot_h;
      }
    }
    if (num_pages > 0 && num_pages % 2 != 0) {
      int last_slot_h = layout_[num_pages - 1].rect.height;
      y += last_slot_h;
    }
  }
  else {
    int cell_h = client.cell.height;
    int gap = 0;
    if (view_mode_ == ViewMode::CONTINUOUS && cell_h > 0) {
      gap = std::max(1, (PAGE_GAP_PX + cell_h - 1) / cell_h) * cell_h;
    }
    int y = 0;
    for (int i = 0; i < num_pages; ++i) {
      auto [page_w, page_h] = doc_.page_size(i);
      float zoom = static_cast<float>(vw) / page_w;
      int h = static_cast<int>(page_h * zoom * user_zoom_);
      layout_[i] = {{0, y, vw, h}, zoom};
      y += h + gap;
    }
  }
}

int App::page_at_y(int y) const {
  int num_pages = static_cast<int>(layout_.size());
  if (num_pages == 0) {
    return 0;
  }

  // Binary search for the page containing global y
  int lo = 0;
  int hi = num_pages - 1;
  while (lo < hi) {
    int mid = lo + (hi - lo + 1) / 2;
    if (layout_[mid].rect.y <= y) {
      lo = mid;
    }
    else {
      hi = mid - 1;
    }
  }
  return lo;
}

float App::effective_render_scale() const {
  RenderScale setting = render_scale_setting_;

  if (setting == RenderScale::AUTO) {
    setting = frontend_->supports_image_viewporting() ? RenderScale::X1 : RenderScale::NEVER;
  }

  if (setting == RenderScale::NEVER) {
    return 0.0f;
  }

  float floor_scale = 1.0f;
  if (setting == RenderScale::X025) {
    floor_scale = 0.25f;
  }
  else if (setting == RenderScale::X05) {
    floor_scale = 0.5f;
  }
  else if (setting == RenderScale::X2) {
    floor_scale = 2.0f;
  }
  else if (setting == RenderScale::X4) {
    floor_scale = 4.0f;
  }

  float dynamic_scale = 0.0f;
  if (user_zoom_ > 1.0f) {
    dynamic_scale = 2.0f;
  }
  if (user_zoom_ > 2.0f) {
    dynamic_scale = 4.0f;
  }
  if (user_zoom_ > 4.0f) {
    dynamic_scale = 8.0f;
  }

  return std::max(floor_scale, dynamic_scale);
}

void App::handle_zoom_change(float old_zoom) {
  auto client = frontend_->client_info();
  int vw = client.viewport_pixel().width;
  int vh = client.viewport_pixel().height;

  int sb_cols = sidebar_effective_width();
  if (sb_cols > 0 && client.cell.width > 0) {
    vw -= sb_cols * client.cell.width;
  }

  // Scale scroll positions to match the new zoom
  if (old_zoom > 0.0f && user_zoom_ != old_zoom) {
    float ratio = user_zoom_ / old_zoom;
    scroll_.y = static_cast<int>(scroll_.y * ratio);
    scroll_.x = static_cast<int>((scroll_.x + vw / 2) * ratio - vw / 2);
  }

  // Rebuild layout with new zoom
  build_layout();

  // Clamp scroll
  if (user_zoom_ <= 1.0f) {
    scroll_.x = 0;
  }
  else {
    scroll_.x = std::max(0, scroll_.x);
  }
  int total_height = document_height();
  int max_y = std::max(0, total_height - vh);
  scroll_.y = std::clamp(scroll_.y, 0, max_y);

  update_viewport();
}

void App::ensure_pages_uploaded(int first, int last) {
  auto client = frontend_->client_info();
  if (!client.is_valid()) {
    return;
  }

  float rs = effective_render_scale();

  for (int i = first; i <= last; ++i) {
    float base_zoom = layout_[i].zoom;

    float render_zoom = (rs == 0.0f) ? base_zoom * user_zoom_ : base_zoom * rs;

    auto it = page_cache_.find(i);
    if (it != page_cache_.end()) {
      if (rs == 0.0f) {
        // NEVER mode: re-render only if zoom changed
        if (it->second.render_zoom == render_zoom) {
          continue;
        }
        frontend_->free_image(it->second.image_id);
        page_cache_.erase(it);
      }
      else {
        // Scaled mode: keep if cached scale is sufficient
        if (it->second.render_scale >= rs) {
          continue;
        }
        // Need higher scale — re-render
        frontend_->free_image(it->second.image_id);
        page_cache_.erase(it);
      }
    }

    frontend_->statusline(std::format("Rendering page {}...", i + 1), "");

    Pixmap pixmap = [&] {
      Stopwatch sw(std::format("mupdf render page {} zoom={} scale={}", i, render_zoom, rs));
      return doc_.render_page(i, render_zoom);
    }();

    Theme eff = effective_theme();
    if (eff == Theme::DARK) {
      pixmap.invert();
    }
    else if (eff == Theme::TERMINAL) {
      auto [fg, bg] = resolve_recolor_colors();
      pixmap.recolor(fg, bg);
    }

    highlight_page_matches(pixmap, i, render_zoom);

    // Grid dimensions based on display size (not rendered pixel size)
    // so images display at correct width in tmux unicode placeholder mode.
    float display_zoom = base_zoom * user_zoom_;
    auto [pw, ph] = doc_.page_size(i);
    int display_w = static_cast<int>(pw * display_zoom);
    int display_h = static_cast<int>(ph * display_zoom);
    int cols = (display_w + client.cell.width - 1) / client.cell.width;
    int rows = (display_h + client.cell.height - 1) / client.cell.height;

    uint32_t image_id = frontend_->upload_image(pixmap, cols, rows);
    size_t mem = static_cast<size_t>(pixmap.width()) * pixmap.height() * pixmap.components();
    page_cache_[i] = {image_id, {pixmap.width(), pixmap.height()}, {cols, rows}, rs, render_zoom, mem};
  }
}

void App::evict_distant_pages(int first, int last) {
  int keep_lo = std::max(0, first - 2);
  int keep_hi = std::min(static_cast<int>(layout_.size()) - 1, last + 2);

  std::vector<int> to_evict;
  for (auto& [page, cached] : page_cache_) {
    if (page < keep_lo || page > keep_hi) {
      to_evict.push_back(page);
    }
  }
  for (int page : to_evict) {
    frontend_->free_image(page_cache_[page].image_id);
    page_cache_.erase(page);
    spdlog::debug("evicted page {}", page);
  }
}

void App::pre_upload_adjacent() {
  int num_pages = static_cast<int>(layout_.size());
  // Try to pre-upload pages in priority order around the viewport
  std::vector<int> candidates = {
      viewport_first_page_ - 1,
      viewport_last_page_ + 1,
      viewport_first_page_ - 2,
      viewport_last_page_ + 2,
  };

  // Add search neighbor pages (lower priority than viewport-adjacent)
  if (!search_results_.empty() && search_current_ >= 0) {
    int n = static_cast<int>(search_results_.size());
    int next_idx = (search_current_ + 1) % n;
    int prev_idx = (search_current_ - 1 + n) % n;
    candidates.push_back(search_results_[next_idx].page);
    candidates.push_back(search_results_[prev_idx].page);
  }

  for (int page : candidates) {
    if (page < 0 || page >= num_pages) {
      continue;
    }
    if (page_cache_.count(page) > 0) {
      continue;
    }
    // Upload one page per idle cycle
    ensure_pages_uploaded(page, page);
    update_statusline();
    return;
  }
}

int App::document_height() const {
  if (layout_.empty()) {
    return 0;
  }
  const auto& last = layout_.back();
  return last.rect.y + last.rect.height;
}

std::pair<std::string, size_t> App::cache_stats() const {
  std::vector<int> pages;
  pages.reserve(page_cache_.size());
  size_t total = 0;
  for (const auto& [page, cached] : page_cache_) {
    pages.push_back(page + 1); // 1-based for display
    total += cached.memory_bytes;
  }
  std::sort(pages.begin(), pages.end());

  // Format as compact ranges: "1-3,5,8-10"
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

void App::update_statusline() {
  std::string left;
  if (input_mode_ == InputMode::COMMAND) {
    left = std::format(":{}", command_input_);
  }
  else if (input_mode_ == InputMode::SEARCH) {
    left = std::format("/{}", search_term_);
  }
  else if (!search_results_.empty() && input_mode_ == InputMode::NORMAL) {
    // Count matches on current page
    int cur = current_page();
    search_page_matches_ = 0;
    for (const auto& hit : search_results_) {
      if (hit.page == cur) {
        ++search_page_matches_;
      }
    }
    search_total_matches_ = static_cast<int>(search_results_.size());
    left = std::format("/{} [{}/{}]", search_term_, search_current_ + 1, search_total_matches_);
  }
  else if (last_action_.active()) {
    left = last_action_.text();
  }

  const char* mode_str = to_label(view_mode_);
  const char* theme_str = to_label(theme_, effective_theme());
  int zoom_pct = static_cast<int>(user_zoom_ * 100.0f + 0.5f);
  int page = current_page() + 1;
  int total = doc_.page_count();
  std::string right = mode_str;
  if (zoom_pct != 100) {
    right += std::format(" \xe2\x94\x82 {}%", zoom_pct);
  }
  right += std::format(" \xe2\x94\x82 {} \xe2\x94\x82 {}/{}", theme_str, page, total);

  if (show_stats_) {
    auto [cached_pages, cached_bytes] = cache_stats();
    if (!cached_pages.empty()) {
      if (cached_bytes >= 1024 * 1024) {
        right += std::format(" \xe2\x94\x82 [{}] {:.1f}M", cached_pages, static_cast<double>(cached_bytes) / (1024.0 * 1024.0));
      }
      else {
        right += std::format(" \xe2\x94\x82 [{}] {:.0f}K", cached_pages, static_cast<double>(cached_bytes) / 1024.0);
      }
    }
  }

  frontend_->statusline(left, right);
}

void App::update_viewport() {
  auto client = frontend_->client_info();
  if (!client.is_valid() || layout_.empty()) {
    spdlog::debug("update_viewport: bail (valid={}, layout={})", client.is_valid(), layout_.size());
    return;
  }

  auto vp = client.viewport_pixel();
  int vh = vp.height;
  int vw = vp.width;

  int sidebar_cols = sidebar_effective_width();
  int sidebar_px = 0;
  if (sidebar_cols > 0 && client.cell.width > 0) {
    sidebar_px = sidebar_cols * client.cell.width;
    vw -= sidebar_px;
  }

  int num_pages = static_cast<int>(layout_.size());
  spdlog::debug("update_viewport: scroll={} vp={} cell={}", scroll_, vp, client.cell);

  // Find visible pages
  int first = page_at_y(scroll_.y);
  int last = first;

  // In SIDE_BY_SIDE, snap back to pair start
  if (view_mode_ == ViewMode::SIDE_BY_SIDE && first > 0 && layout_[first - 1].rect.y == layout_[first].rect.y) {
    first = first - 1;
  }

  for (int i = first; i < num_pages; ++i) {
    if (layout_[i].rect.y >= scroll_.y + vh) {
      break;
    }
    last = i;
  }

  viewport_first_page_ = first;
  viewport_last_page_ = last;

  evict_distant_pages(first, last);
  ensure_pages_uploaded(first, last);

  // Build PageSlice vector
  std::vector<PageSlice> slices;
  for (int i = first; i <= last; ++i) {
    auto it = page_cache_.find(i);
    if (it == page_cache_.end()) {
      continue;
    }
    const auto& cached = it->second;
    const auto& lay = layout_[i];

    int page_top = lay.rect.y - scroll_.y;

    // Zoomed page dimensions (what the user sees on screen)
    auto [page_w, page_h] = doc_.page_size(i);
    int zoomed_w = static_cast<int>(page_w * lay.zoom * user_zoom_);
    int zoomed_h = static_cast<int>(page_h * lay.zoom * user_zoom_);

    // Scale factor: zoomed-content pixels -> image pixels
    float img_scale;
    if (cached.render_scale == 0.0f) {
      img_scale = 1.0f; // NEVER mode: image IS the zoomed content
    }
    else {
      img_scale = cached.render_scale / user_zoom_;
    }

    // Where the actual rendered content starts in viewport coordinates.
    int content_top = page_top;
    int dst_col = sidebar_cols;

    if (view_mode_ == ViewMode::PAGE_HEIGHT) {
      int vert_pad = std::max(0, (lay.rect.height - zoomed_h) / 2);
      content_top = page_top + vert_pad;
      if (zoomed_w < vw) {
        dst_col = sidebar_cols + (vw - zoomed_w) / 2 / client.cell.width;
      }
    }
    else if (view_mode_ == ViewMode::SIDE_BY_SIDE) {
      int vert_pad = std::max(0, (lay.rect.height - zoomed_h) / 2);
      content_top = page_top + vert_pad;
      bool is_last_odd = (i == num_pages - 1) && (i % 2 == 0);
      if (is_last_odd) {
        if (zoomed_w < vw) {
          dst_col = sidebar_cols + (vw - zoomed_w) / 2 / client.cell.width;
        }
      }
      else if (i % 2 == 0) {
        int half_w = vw / 2;
        if (zoomed_w < half_w) {
          dst_col = sidebar_cols + (half_w - zoomed_w) / 2 / client.cell.width;
        }
      }
      else {
        int half_w = vw / 2;
        if (zoomed_w < half_w) {
          dst_col = sidebar_cols + (half_w + (half_w - zoomed_w) / 2) / client.cell.width;
        }
        else {
          dst_col = sidebar_cols + half_w / client.cell.width;
        }
      }
    }
    else {
      // CONTINUOUS / PAGE_WIDTH: center when zoomed content is narrower than viewport
      if (zoomed_w < vw) {
        dst_col = sidebar_cols + (vw - zoomed_w) / 2 / client.cell.width;
      }
    }

    // Clamp to viewport
    PixelRect content_rect = {0, content_top, zoomed_w, zoomed_h};
    PixelRect viewport_rect = {0, 0, vw, vh};
    auto visible = content_rect.intersect(viewport_rect);
    if (visible.height <= 0) {
      continue;
    }

    // Source rect in image pixel coordinates
    int max_scroll_x = std::max(0, zoomed_w - vw);
    int clamped_scroll_x = std::clamp(scroll_.x, 0, max_scroll_x);
    int src_x = static_cast<int>(clamped_scroll_x * img_scale);
    int src_y = static_cast<int>((visible.top() - content_top) * img_scale);
    int src_w = static_cast<int>(std::min(zoomed_w, vw) * img_scale);
    int src_h = static_cast<int>(visible.height * img_scale);

    // Clamp source rect to actual image dimensions
    src_x = std::min(src_x, std::max(0, cached.pixel_size.width - 1));
    src_y = std::min(src_y, std::max(0, cached.pixel_size.height - 1));
    src_w = std::min(src_w, cached.pixel_size.width - src_x);
    src_h = std::min(src_h, cached.pixel_size.height - src_y);

    PixelRect src = {src_x, src_y, src_w, src_h};

    // Destination in cells
    int visible_px_w = std::min(zoomed_w, vw);
    int dst_cols = (visible_px_w + client.cell.width - 1) / client.cell.width;
    int dst_row = visible.top() / client.cell.height;
    int dst_rows = (visible.bottom() + client.cell.height - 1) / client.cell.height - dst_row;
    CellRect dst = {dst_col, dst_row, dst_cols, dst_rows};

    slices.push_back({cached.image_id, src, dst, cached.cell_grid, cached.pixel_size});
  }

  spdlog::debug("update_viewport: pages [{},{}] slices={}", first, last, slices.size());
  frontend_->show_pages(slices);
  update_sidebar();
  update_statusline();
}

int App::current_page() const {
  return page_at_y(scroll_.y);
}

void App::scroll(int dx, int dy) {
  if (layout_.empty()) {
    return;
  }

  auto client = frontend_->client_info();
  int vh = client.viewport_pixel().height;
  int vw = client.viewport_pixel().width;

  int sb_cols = sidebar_effective_width();
  if (sb_cols > 0 && client.cell.width > 0) {
    vw -= sb_cols * client.cell.width;
  }

  int total_height = document_height();
  int max_y = std::max(0, total_height - vh);

  int new_y = std::clamp(scroll_.y + dy, 0, max_y);

  // In non-continuous modes, clamp scroll to stay within the current page
  if (view_mode_ != ViewMode::CONTINUOUS) {
    int p = page_at_y(new_y);
    int page_top = layout_[p].rect.y;
    int page_max = page_top + std::max(0, layout_[p].rect.height - vh);
    new_y = std::clamp(new_y, page_top, page_max);
  }

  // Clamp horizontal scroll based on zoomed page width
  int p = page_at_y(new_y);
  auto [pw, ph] = doc_.page_size(p);
  int zoomed_w = static_cast<int>(pw * layout_[p].zoom * user_zoom_);
  int max_x = std::max(0, zoomed_w - vw);
  int new_x = std::clamp(scroll_.x + dx, 0, max_x);

  if (new_x == scroll_.x && new_y == scroll_.y) {
    return;
  }

  scroll_.x = new_x;
  scroll_.y = new_y;
  update_viewport();
}

void App::render() {
  spdlog::debug("render: clearing {} cached pages", page_cache_.size());
  for (auto& [page, cached] : page_cache_) {
    frontend_->free_image(cached.image_id);
  }
  page_cache_.clear();

  build_layout();

  // Clamp scroll
  if (!layout_.empty()) {
    auto client = frontend_->client_info();
    int vh = client.viewport_pixel().height;
    int vw = client.viewport_pixel().width;

    int sb_cols = sidebar_effective_width();
    if (sb_cols > 0 && client.cell.width > 0) {
      vw -= sb_cols * client.cell.width;
    }

    int total_height = document_height();
    int max_y = std::max(0, total_height - vh);
    scroll_.y = std::clamp(scroll_.y, 0, max_y);

    if (user_zoom_ <= 1.0f) {
      scroll_.x = 0;
    }
    else {
      int p = page_at_y(scroll_.y);
      auto [pw, ph] = doc_.page_size(p);
      int zoomed_w = static_cast<int>(pw * layout_[p].zoom * user_zoom_);
      int max_x = std::max(0, zoomed_w - vw);
      scroll_.x = std::clamp(scroll_.x, 0, max_x);
    }
  }

  update_viewport();
}

void App::jump_to_page(int page) {
  push_jump_history();
  int num_pages = static_cast<int>(layout_.size());
  page = std::clamp(page, 0, num_pages - 1);
  // In SIDE_BY_SIDE, snap to even page index (pair start)
  if (view_mode_ == ViewMode::SIDE_BY_SIDE && page % 2 != 0) {
    page = page - 1;
  }
  scroll_.y = layout_[page].rect.y;

  // Clamp
  auto client = frontend_->client_info();
  int vh = client.viewport_pixel().height;
  int total_height = document_height();
  int max_y = std::max(0, total_height - vh);
  scroll_.y = std::clamp(scroll_.y, 0, max_y);

  update_viewport();
}

void App::initialize() {
  frontend_->clear();
  render();
  last_activity_time_ = std::chrono::steady_clock::now();
}

void App::idle_tick() {
  auto client = frontend_->client_info();
  if (client.pixel != layout_size_) {
    spdlog::info("idle resize: pixel {} -> {}", layout_size_, client.pixel);
    handle_command(cmd::Resize{});
  }
  else {
    auto idle = std::chrono::steady_clock::now() - last_activity_time_;
    if (idle >= std::chrono::milliseconds(300)) {
      pre_upload_adjacent();
    }
    if (last_action_.expired()) {
      last_action_.clear();
      update_statusline();
    }
  }
}

const Outline& App::outline() {
  if (outline_.empty()) {
    outline_ = doc_.load_outline();
  }
  return outline_;
}

std::vector<PageLink> App::visible_links() const {
  std::vector<PageLink> all_links;
  for (int p = viewport_first_page_; p <= viewport_last_page_; ++p) {
    auto page_links = doc_.load_links(p);
    all_links.insert(all_links.end(), page_links.begin(), page_links.end());
  }
  return all_links;
}

void App::handle_command(const RpcCommand& cmd) {
  last_activity_time_ = std::chrono::steady_clock::now();

  std::visit(
      [&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, cmd::Quit>) {
          spdlog::info("quit");
          running_ = false;
        }
        else if constexpr (std::is_same_v<T, cmd::Resize>) {
          auto client = frontend_->client_info();
          spdlog::info("resize: {} px, {} cell", client.pixel, client.cell);
          frontend_->clear();
          render();
        }
        else if constexpr (std::is_same_v<T, cmd::ScrollDown>) {
          auto client = frontend_->client_info();
          int count = std::max(1, arg.count);
          scroll(0, client.cell.height * scroll_lines_ * count);
        }
        else if constexpr (std::is_same_v<T, cmd::ScrollUp>) {
          auto client = frontend_->client_info();
          int count = std::max(1, arg.count);
          scroll(0, -client.cell.height * scroll_lines_ * count);
        }
        else if constexpr (std::is_same_v<T, cmd::HalfPageDown>) {
          auto client = frontend_->client_info();
          scroll(0, client.viewport_pixel().height / 2);
        }
        else if constexpr (std::is_same_v<T, cmd::HalfPageUp>) {
          auto client = frontend_->client_info();
          scroll(0, -client.viewport_pixel().height / 2);
        }
        else if constexpr (std::is_same_v<T, cmd::PageDown>) {
          if (!layout_.empty()) {
            int p = current_page();
            if (view_mode_ == ViewMode::SIDE_BY_SIDE) {
              jump_to_page(p + 2);
            }
            else if (view_mode_ != ViewMode::CONTINUOUS) {
              jump_to_page(p + 1);
            }
            else {
              scroll(0, layout_[p].rect.height);
            }
          }
        }
        else if constexpr (std::is_same_v<T, cmd::PageUp>) {
          if (!layout_.empty()) {
            int p = current_page();
            if (view_mode_ == ViewMode::SIDE_BY_SIDE) {
              jump_to_page(p - 2);
            }
            else if (view_mode_ != ViewMode::CONTINUOUS) {
              jump_to_page(p - 1);
            }
            else {
              scroll(0, -layout_[p].rect.height);
            }
          }
        }
        else if constexpr (std::is_same_v<T, cmd::ScrollLeft>) {
          auto client = frontend_->client_info();
          int count = std::max(1, arg.count);
          scroll(-client.cell.width * scroll_lines_ * count, 0);
        }
        else if constexpr (std::is_same_v<T, cmd::ScrollRight>) {
          auto client = frontend_->client_info();
          int count = std::max(1, arg.count);
          scroll(client.cell.width * scroll_lines_ * count, 0);
        }
        else if constexpr (std::is_same_v<T, cmd::GotoPage>) {
          jump_to_page(arg.page - 1);
        }
        else if constexpr (std::is_same_v<T, cmd::GotoFirstPage>) {
          spdlog::info("goto first page");
          jump_to_page(0);
        }
        else if constexpr (std::is_same_v<T, cmd::GotoLastPage>) {
          spdlog::info("goto last page (bottom)");
          if (!layout_.empty()) {
            auto client = frontend_->client_info();
            int vh = client.viewport_pixel().height;
            int total_height = document_height();
            push_jump_history();
            scroll_.y = std::max(0, total_height - vh);
            update_viewport();
          }
        }
        else if constexpr (std::is_same_v<T, cmd::ZoomIn>) {
          static constexpr float LEVELS[] = {1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
          float old = user_zoom_;
          for (float lvl : LEVELS) {
            if (lvl > user_zoom_ + 0.01f) {
              user_zoom_ = lvl;
              break;
            }
          }
          spdlog::info("zoom in: {:.0f}%", user_zoom_ * 100.0f);
          handle_zoom_change(old);
        }
        else if constexpr (std::is_same_v<T, cmd::ZoomOut>) {
          static constexpr float LEVELS[] = {1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
          float old = user_zoom_;
          float best = 1.0f;
          for (float lvl : LEVELS) {
            if (lvl < user_zoom_ - 0.01f) {
              best = lvl;
            }
          }
          user_zoom_ = best;
          spdlog::info("zoom out: {:.0f}%", user_zoom_ * 100.0f);
          handle_zoom_change(old);
        }
        else if constexpr (std::is_same_v<T, cmd::ZoomReset>) {
          float old = user_zoom_;
          user_zoom_ = 1.0f;
          scroll_.x = 0;
          spdlog::info("zoom reset: 100%");
          handle_zoom_change(old);
        }
        else if constexpr (std::is_same_v<T, cmd::ToggleViewMode>) {
          switch (view_mode_) {
            case ViewMode::CONTINUOUS:
              view_mode_ = ViewMode::PAGE_WIDTH;
              break;
            case ViewMode::PAGE_WIDTH:
              view_mode_ = ViewMode::PAGE_HEIGHT;
              break;
            case ViewMode::PAGE_HEIGHT:
              view_mode_ = ViewMode::SIDE_BY_SIDE;
              break;
            case ViewMode::SIDE_BY_SIDE:
              view_mode_ = ViewMode::CONTINUOUS;
              break;
          }
          user_zoom_ = 1.0f;
          scroll_.x = 0;
          render();
        }
        else if constexpr (std::is_same_v<T, cmd::SetViewMode>) {
          command_input_ = "set mode " + arg.mode;
          execute_command();
          command_input_.clear();
        }
        else if constexpr (std::is_same_v<T, cmd::ToggleTheme>) {
          if (theme_ == Theme::DARK) {
            theme_ = Theme::LIGHT;
          }
          else if (theme_ == Theme::LIGHT) {
            theme_ = Theme::TERMINAL;
          }
          else {
            theme_ = Theme::DARK;
          }
          frontend_->clear();
          render();
        }
        else if constexpr (std::is_same_v<T, cmd::SetTheme>) {
          command_input_ = "set theme " + arg.theme;
          execute_command();
          command_input_.clear();
        }
        else if constexpr (std::is_same_v<T, cmd::SetRenderScale>) {
          command_input_ = "set render-scale " + arg.strategy;
          execute_command();
          command_input_.clear();
        }
        else if constexpr (std::is_same_v<T, cmd::Reload>) {
          command_input_ = "reload";
          execute_command();
          command_input_.clear();
        }
        else if constexpr (std::is_same_v<T, cmd::JumpBack>) {
          if (!jump_history_.empty()) {
            if (jump_index_ < 0) {
              jump_history_.push_back({scroll_.x, scroll_.y});
              jump_index_ = static_cast<int>(jump_history_.size()) - 2;
            }
            else if (jump_index_ > 0) {
              --jump_index_;
            }
            else {
              return;
            }
            scroll_.x = jump_history_[jump_index_].scroll_x;
            scroll_.y = jump_history_[jump_index_].scroll_y;
            update_viewport();
          }
        }
        else if constexpr (std::is_same_v<T, cmd::JumpForward>) {
          if (jump_index_ >= 0) {
            ++jump_index_;
            if (jump_index_ >= static_cast<int>(jump_history_.size()) - 1) {
              scroll_.x = jump_history_.back().scroll_x;
              scroll_.y = jump_history_.back().scroll_y;
              jump_history_.pop_back();
              jump_index_ = -1;
            }
            else {
              scroll_.x = jump_history_[jump_index_].scroll_x;
              scroll_.y = jump_history_[jump_index_].scroll_y;
            }
            update_viewport();
          }
        }
        else if constexpr (std::is_same_v<T, cmd::Search>) {
          search_term_ = arg.term;
          execute_search();
        }
        else if constexpr (std::is_same_v<T, cmd::SearchNext>) {
          if (!search_results_.empty()) {
            search_navigate(1);
          }
        }
        else if constexpr (std::is_same_v<T, cmd::SearchPrev>) {
          if (!search_results_.empty()) {
            search_navigate(-1);
          }
        }
        else if constexpr (std::is_same_v<T, cmd::ClearSearch>) {
          clear_search();
        }
        else if constexpr (std::is_same_v<T, cmd::EnterLinkHints>) {
          enter_link_hints();
        }
        else if constexpr (std::is_same_v<T, cmd::LinkHintKey>) {
          if (input_mode_ == InputMode::LINK_HINTS) {
            link_hint_input_ += arg.ch;
            std::vector<ActiveLinkHint> matches;
            for (const auto& hint : link_hints_) {
              if (hint.label.substr(0, link_hint_input_.size()) == link_hint_input_) {
                matches.push_back(hint);
              }
            }
            if (matches.size() == 1) {
              follow_link(matches[0].link);
            }
            else if (matches.empty()) {
              exit_link_hints();
            }
            else {
              render_link_hints();
            }
          }
        }
        else if constexpr (std::is_same_v<T, cmd::LinkHintCancel>) {
          if (input_mode_ == InputMode::LINK_HINTS) {
            exit_link_hints();
          }
        }
        else if constexpr (std::is_same_v<T, cmd::GetOutline>) {
          if (outline_.empty()) {
            outline_ = doc_.load_outline();
          }
        }
        else if constexpr (std::is_same_v<T, cmd::GetLinks>) {
          // Data is fetched by the caller via visible_links()
        }
        else if constexpr (std::is_same_v<T, cmd::GetState>) {
          // Data is fetched by the caller via view_state()
        }
        else if constexpr (std::is_same_v<T, cmd::OpenOutline>) {
          if (outline_.empty()) {
            outline_ = doc_.load_outline();
          }
          if (outline_.empty()) {
            last_action_.set("No outline");
          }
          else {
            input_mode_ = InputMode::OUTLINE;
            outline_cursor_ = 0;
            outline_scroll_ = 0;
            outline_filter_.clear();
            outline_apply_filter();
            show_outline_popup();
          }
        }
        else if constexpr (std::is_same_v<T, cmd::OutlineNavigate>) {
          outline_navigate(arg.delta);
        }
        else if constexpr (std::is_same_v<T, cmd::OutlineFilterChar>) {
          outline_filter_ += arg.ch;
          outline_apply_filter();
          show_outline_popup();
        }
        else if constexpr (std::is_same_v<T, cmd::OutlineFilterBackspace>) {
          if (!outline_filter_.empty()) {
            outline_filter_.pop_back();
            outline_apply_filter();
            show_outline_popup();
          }
        }
        else if constexpr (std::is_same_v<T, cmd::OutlineJump>) {
          outline_jump();
        }
        else if constexpr (std::is_same_v<T, cmd::CloseOutline>) {
          input_mode_ = InputMode::NORMAL;
          frontend_->clear_overlay();
          update_viewport();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::ToggleSidebar>) {
          if (sidebar_visible_) {
            sidebar_visible_ = false;
            sidebar_filter_.clear();
            frontend_->clear();
            render();
          }
          else {
            if (outline_.empty()) {
              outline_ = doc_.load_outline();
            }
            if (outline_.empty()) {
              last_action_.set("No outline");
            }
            else {
              sidebar_visible_ = true;
              sidebar_scroll_ = 0;
              input_mode_ = InputMode::SIDEBAR;
              sidebar_filter_.clear();
              sidebar_apply_filter();
              int active = active_outline_index();
              if (active >= 0) {
                for (int fi = 0; fi < static_cast<int>(sidebar_filtered_.size()); ++fi) {
                  if (sidebar_filtered_[fi] == active) {
                    sidebar_cursor_ = fi;
                    break;
                  }
                }
              }
              frontend_->clear();
              render();
            }
          }
        }
        else if constexpr (std::is_same_v<T, cmd::SidebarUnfocus>) {
          input_mode_ = InputMode::NORMAL;
          sidebar_filter_.clear();
          sidebar_apply_filter();
          update_sidebar();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::SidebarClose>) {
          input_mode_ = InputMode::NORMAL;
          sidebar_visible_ = false;
          sidebar_filter_.clear();
          frontend_->clear();
          render();
        }
        else if constexpr (std::is_same_v<T, cmd::SidebarNavigate>) {
          sidebar_navigate(arg.delta);
        }
        else if constexpr (std::is_same_v<T, cmd::SidebarFilterChar>) {
          sidebar_filter_ += arg.ch;
          sidebar_apply_filter();
          update_sidebar();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::SidebarFilterBackspace>) {
          if (!sidebar_filter_.empty()) {
            sidebar_filter_.pop_back();
            sidebar_apply_filter();
            update_sidebar();
            update_statusline();
          }
        }
        else if constexpr (std::is_same_v<T, cmd::SidebarJump>) {
          sidebar_jump();
        }
        else if constexpr (std::is_same_v<T, cmd::EnterCommandMode>) {
          input_mode_ = InputMode::COMMAND;
          command_input_.clear();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::CommandChar>) {
          command_input_ += arg.ch;
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::CommandBackspace>) {
          if (!command_input_.empty()) {
            command_input_.pop_back();
          }
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::CommandExecute>) {
          execute_command();
          input_mode_ = InputMode::NORMAL;
          command_input_.clear();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::CommandCancel>) {
          input_mode_ = InputMode::NORMAL;
          command_input_.clear();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::EnterSearchMode>) {
          input_mode_ = InputMode::SEARCH;
          search_term_.clear();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::SearchChar>) {
          search_term_ += arg.ch;
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::SearchBackspace>) {
          if (!search_term_.empty()) {
            search_term_.pop_back();
          }
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::SearchExecute>) {
          execute_search();
          input_mode_ = InputMode::NORMAL;
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::SearchCancel>) {
          input_mode_ = InputMode::NORMAL;
          search_term_.clear();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::ShowHelp>) {
          input_mode_ = InputMode::HELP;
          show_help();
        }
        else if constexpr (std::is_same_v<T, cmd::DismissOverlay>) {
          input_mode_ = InputMode::NORMAL;
          frontend_->clear_overlay();
          update_viewport();
          update_statusline();
        }
        else if constexpr (std::is_same_v<T, cmd::Hide>) {
          frontend_->show_pages({});
        }
        else if constexpr (std::is_same_v<T, cmd::Show>) {
          update_viewport();
        }
        else if constexpr (std::is_same_v<T, cmd::MouseScroll>) {
          scroll(arg.dx, arg.dy);
        }
        else if constexpr (std::is_same_v<T, cmd::ClickAt>) {
          auto client = frontend_->client_info();
          if (!client.is_valid() || layout_.empty() || client.cell.width == 0 || client.cell.height == 0) {
            return;
          }

          int sidebar_cols = sidebar_effective_width();
          int sidebar_px = 0;
          if (sidebar_cols > 0 && client.cell.width > 0) {
            sidebar_px = sidebar_cols * client.cell.width;
          }

          // Screen cell → viewport pixel → global pixel
          int vp_x = arg.col * client.cell.width;
          int vp_y = arg.row * client.cell.height;
          int global_x = vp_x - sidebar_px + scroll_.x;
          int global_y = vp_y + scroll_.y;

          // Find which page this point is on
          int p = page_at_y(global_y);
          if (p < 0 || p >= static_cast<int>(layout_.size())) {
            return;
          }

          // Convert to page point coordinates
          float page_zoom = layout_[p].zoom * user_zoom_;
          if (page_zoom <= 0.0f) {
            return;
          }
          float pt_x = static_cast<float>(global_x - layout_[p].rect.x) / page_zoom;
          float pt_y = static_cast<float>(global_y - layout_[p].rect.y) / page_zoom;

          // Hit-test against page links
          auto page_links = doc_.load_links(p);
          for (auto& pl : page_links) {
            if (pt_x >= pl.x && pt_x <= pl.x + pl.w && pt_y >= pl.y && pt_y <= pl.y + pl.h) {
              follow_link(pl);
              return;
            }
          }
        }
      },
      cmd
  );
}

void App::execute_command() {
  std::string cmd = command_input_;

  // Trim leading/trailing whitespace
  auto start = cmd.find_first_not_of(' ');
  if (start == std::string::npos) {
    return;
  }
  cmd = cmd.substr(start);
  auto end = cmd.find_last_not_of(' ');
  cmd = cmd.substr(0, end + 1);

  // Bare number → goto page
  try {
    size_t pos = 0;
    int page = std::stoi(cmd, &pos);
    if (pos == cmd.size()) {
      jump_to_page(page - 1);
      return;
    }
  }
  catch (...) {
  }

  // Split into command name + args
  auto space = cmd.find(' ');
  std::string name = (space != std::string::npos) ? cmd.substr(0, space) : cmd;
  std::string args = (space != std::string::npos) ? cmd.substr(space + 1) : "";

  // Trim args
  auto args_start = args.find_first_not_of(' ');
  if (args_start != std::string::npos) {
    args = args.substr(args_start);
  }

  if (name == "goto" || name == "g") {
    try {
      int page = std::stoi(args);
      jump_to_page(page - 1);
    }
    catch (...) {
      last_action_.set("Invalid page number");
    }
  }
  else if (name == "q" || name == "quit") {
    running_ = false;
  }
  else if (name == "reload") {
    try {
      // Free all cached images
      for (auto& [page, cached] : page_cache_) {
        frontend_->free_image(cached.image_id);
      }
      page_cache_.clear();

      doc_.reload(file_path_);

      // Clear derived state
      outline_.clear();
      sidebar_filtered_.clear();
      search_results_.clear();
      search_term_.clear();
      search_current_ = -1;

      // Rebuild and render
      build_layout();

      auto client = frontend_->client_info();
      int vh = client.viewport_pixel().height;
      int total_height = document_height();
      int max_y = std::max(0, total_height - vh);
      scroll_.y = std::clamp(scroll_.y, 0, max_y);

      frontend_->clear();
      update_viewport();
      last_action_.set("Reloaded");
    }
    catch (const std::runtime_error& e) {
      last_action_.set("Reload failed: {}", e.what());
    }
  }
  else if (name == "set") {
    auto key_space = args.find(' ');
    std::string key = (key_space != std::string::npos) ? args.substr(0, key_space) : args;
    std::string value = (key_space != std::string::npos) ? args.substr(key_space + 1) : "";

    // Trim value
    auto val_start = value.find_first_not_of(' ');
    if (val_start != std::string::npos) {
      value = value.substr(val_start);
    }
    auto val_end = value.find_last_not_of(' ');
    if (val_end != std::string::npos) {
      value = value.substr(0, val_end + 1);
    }

    if (key == "theme") {
      auto th = parse_theme(value);
      if (th) {
        theme_ = *th;
        frontend_->clear();
        render();
      }
      else {
        last_action_.set("Unknown theme: {}", value);
      }
    }
    else if (key == "mode") {
      auto vm = parse_view_mode(value);
      if (!vm) {
        last_action_.set("Unknown mode: {}", value);
        return;
      }
      view_mode_ = *vm;
      user_zoom_ = 1.0f;
      scroll_.x = 0;
      render();
    }
    else if (key == "render-scale") {
      if (value == "auto") {
        render_scale_setting_ = RenderScale::AUTO;
      }
      else if (value == "never") {
        render_scale_setting_ = RenderScale::NEVER;
      }
      else if (value == "0.25") {
        render_scale_setting_ = RenderScale::X025;
      }
      else if (value == "0.5") {
        render_scale_setting_ = RenderScale::X05;
      }
      else if (value == "1") {
        render_scale_setting_ = RenderScale::X1;
      }
      else if (value == "2") {
        render_scale_setting_ = RenderScale::X2;
      }
      else if (value == "4") {
        render_scale_setting_ = RenderScale::X4;
      }
      else {
        last_action_.set("Unknown render-scale: {}", value);
        return;
      }
      frontend_->clear();
      render();
    }
    else if (key == "sidebar-width") {
      try {
        int w = std::stoi(value);
        if (w < 0) {
          last_action_.set("Invalid width");
        }
        else {
          sidebar_width_cols_ = w;
          if (sidebar_visible_) {
            frontend_->clear();
            render();
          }
        }
      }
      catch (...) {
        last_action_.set("Invalid width: {}", value);
      }
    }
    else {
      last_action_.set("Unknown setting: {}", key);
    }
  }
  else {
    last_action_.set("Unknown: {}", name);
  }
}

void App::show_help() {
  const auto& bindings = get_help_bindings();

  int max_key_len = 0;
  for (const auto& hb : bindings) {
    max_key_len = std::max(max_key_len, static_cast<int>(std::strlen(hb.key_label)));
  }

  auto format_line = [&](const char* key, const char* desc) {
    std::string line = key;
    line += std::string(max_key_len - static_cast<int>(std::strlen(key)) + 3, ' ');
    line += desc;
    return line;
  };

  std::vector<std::string> lines;
  lines.emplace_back("Key Bindings");
  lines.emplace_back("");

  for (const auto& hb : bindings) {
    lines.push_back(format_line(hb.key_label, hb.description));
  }

  frontend_->show_overlay("Help", lines);
}

bool App::fuzzy_match(const std::string& text, const std::string& pattern) {
  size_t ti = 0;
  for (size_t pi = 0; pi < pattern.size(); ++pi) {
    char pc = static_cast<char>(std::tolower(static_cast<unsigned char>(pattern[pi])));
    bool found = false;
    while (ti < text.size()) {
      char tc = static_cast<char>(std::tolower(static_cast<unsigned char>(text[ti])));
      ++ti;
      if (tc == pc) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

void App::outline_apply_filter() {
  filtered_indices_.clear();
  for (int i = 0; i < static_cast<int>(outline_.size()); ++i) {
    if (outline_filter_.empty() || fuzzy_match(outline_[i].title, outline_filter_)) {
      filtered_indices_.push_back(i);
    }
  }
  outline_cursor_ = 0;
  outline_scroll_ = 0;
}

void App::outline_navigate(int delta) {
  if (filtered_indices_.empty()) {
    return;
  }
  outline_cursor_ = std::clamp(outline_cursor_ + delta, 0, static_cast<int>(filtered_indices_.size()) - 1);
  show_outline_popup();
}

void App::outline_jump() {
  if (filtered_indices_.empty()) {
    return;
  }
  int page = outline_[filtered_indices_[outline_cursor_]].page;
  input_mode_ = InputMode::NORMAL;
  frontend_->clear_overlay();
  // jump_to_page already calls push_jump_history()
  jump_to_page(page);
  update_statusline();
}

void App::show_outline_popup() {
  auto client = frontend_->client_info();

  // Fixed 75% of terminal dimensions (minus status bar row and gap row)
  int box_lines = std::max(4, client.rows * 3 / 4 - 2);
  int box_width = std::max(20, client.cols * 3 / 4);

  // Header: filter line + separator line
  int max_visible = box_lines - 2;

  // Scroll window follows cursor
  if (outline_cursor_ < outline_scroll_) {
    outline_scroll_ = outline_cursor_;
  }
  if (outline_cursor_ >= outline_scroll_ + max_visible) {
    outline_scroll_ = outline_cursor_ - max_visible + 1;
  }

  // Pad a line to box_width (accounting for ANSI escapes not taking display space).
  // The overlay adds borders: │ SP content SP │ → 3 chars each side = 6 total.
  int content_w = box_width - 6;
  auto pad_line = [content_w](const std::string& text, int visible_len) -> std::string {
    if (visible_len >= content_w) {
      return text;
    }
    return text + std::string(content_w - visible_len, ' ');
  };

  // Truncate a string to fit within max_cols display columns.
  auto truncate_to = [](const std::string& text, int max_cols) -> std::string {
    int cols = 0;
    size_t pos = 0;
    while (pos < text.size() && cols < max_cols) {
      auto c = static_cast<unsigned char>(text[pos]);
      if (c == 0x1B && pos + 1 < text.size() && text[pos + 1] == '[') {
        pos += 2;
        while (pos < text.size() && static_cast<unsigned char>(text[pos]) < 0x40) {
          ++pos;
        }
        if (pos < text.size()) {
          ++pos;
        }
        continue;
      }
      size_t char_len = 1;
      if (c >= 0xF0) {
        char_len = 4;
      }
      else if (c >= 0xE0) {
        char_len = 3;
      }
      else if (c >= 0x80) {
        char_len = 2;
      }
      pos += char_len;
      ++cols;
    }
    return text.substr(0, pos);
  };

  std::string border_title = "Table of Contents";

  std::vector<std::string> lines;

  auto filter_line = std::format("> {}", outline_filter_);
  lines.push_back(pad_line(filter_line, static_cast<int>(filter_line.size())));
  // Separator: thin horizontal line
  std::string sep;
  for (int i = 0; i < content_w; ++i) {
    sep += "\xe2\x94\x80"; // ─
  }
  lines.push_back(sep);

  int end = std::min(outline_scroll_ + max_visible, static_cast<int>(filtered_indices_.size()));
  for (int vi = outline_scroll_; vi < end; ++vi) {
    const auto& entry = outline_[filtered_indices_[vi]];
    std::string indent(entry.level * 2, ' ');
    auto page_str = std::to_string(entry.page + 1);
    auto title_part = std::format("{}{}", indent, entry.title);
    int title_len = static_cast<int>(title_part.size());
    int page_len = static_cast<int>(page_str.size());

    // Truncate title with ellipsis if it would overflow content_w
    int max_title = content_w - page_len - 1;
    if (title_len > max_title && max_title > 3) {
      title_part = truncate_to(title_part, max_title - 3) + "...";
      title_len = max_title;
    }

    int gap = std::max(1, content_w - title_len - page_len);
    auto visible_text = std::format("{}{:>{}}{}", title_part, "", gap, page_str);

    if (vi == outline_cursor_) {
      auto line = std::format("{}{}", sgr::BOLD, visible_text);
      lines.push_back(line);
    }
    else {
      lines.push_back(visible_text);
    }
  }

  if (filtered_indices_.empty()) {
    lines.push_back(pad_line("  (no matches)", 14));
  }

  // Pad remaining rows to fill the fixed height
  while (static_cast<int>(lines.size()) < box_lines) {
    lines.push_back(pad_line("", 0));
  }

  frontend_->show_overlay(border_title, lines);
}

int App::sidebar_effective_width() const {
  if (!sidebar_visible_) {
    return 0;
  }
  auto client = frontend_->client_info();
  if (sidebar_width_cols_ > 0) {
    return std::min(sidebar_width_cols_, client.cols - 10);
  }
  return std::max(15, client.cols / 5);
}

int App::active_outline_index() const {
  int cp = current_page();
  int best = -1;
  for (int i = 0; i < static_cast<int>(outline_.size()); ++i) {
    if (outline_[i].page <= cp) {
      best = i;
    }
  }
  return best;
}

void App::sidebar_apply_filter() {
  sidebar_filtered_.clear();
  for (int i = 0; i < static_cast<int>(outline_.size()); ++i) {
    if (sidebar_filter_.empty() || fuzzy_match(outline_[i].title, sidebar_filter_)) {
      sidebar_filtered_.push_back(i);
    }
  }
  sidebar_cursor_ = 0;
  sidebar_scroll_ = 0;
}

void App::sidebar_navigate(int delta) {
  if (sidebar_filtered_.empty()) {
    return;
  }
  sidebar_cursor_ = std::clamp(sidebar_cursor_ + delta, 0, static_cast<int>(sidebar_filtered_.size()) - 1);
  update_sidebar();
  update_statusline();
}

void App::sidebar_jump() {
  if (sidebar_filtered_.empty()) {
    return;
  }
  int page = outline_[sidebar_filtered_[sidebar_cursor_]].page;
  input_mode_ = InputMode::NORMAL;
  sidebar_filter_.clear();
  sidebar_apply_filter();
  jump_to_page(page);
}

void App::execute_search() {
  if (search_term_.empty()) {
    return;
  }

  search_results_ = doc_.search(search_term_);
  search_total_matches_ = static_cast<int>(search_results_.size());

  if (search_results_.empty()) {
    search_current_ = -1;
    last_action_.set("Pattern not found: {}", search_term_);
    return;
  }

  // Check if any hit is already visible in the viewport
  auto client = frontend_->client_info();
  int vh = client.viewport_pixel().height;
  int first_visible = -1;
  for (int i = 0; i < static_cast<int>(search_results_.size()); ++i) {
    const auto& hit = search_results_[i];
    if (hit.page < viewport_first_page_ || hit.page > viewport_last_page_) {
      continue;
    }
    float page_zoom = layout_[hit.page].zoom * user_zoom_;
    int hit_global_y = layout_[hit.page].rect.y + static_cast<int>(hit.y * page_zoom);
    if (hit_global_y >= scroll_.y && hit_global_y < scroll_.y + vh) {
      first_visible = i;
      break;
    }
  }

  if (first_visible >= 0) {
    search_current_ = first_visible;
  }
  else {
    // Find first match on or after current page
    int cur = current_page();
    search_current_ = 0;
    for (int i = 0; i < static_cast<int>(search_results_.size()); ++i) {
      if (search_results_[i].page >= cur) {
        search_current_ = i;
        break;
      }
    }
    scroll_to_search_hit();
  }

  frontend_->clear();
  render();
}

void App::clear_search() {
  search_results_.clear();
  search_term_.clear();
  search_current_ = -1;
  search_page_matches_ = 0;
  search_total_matches_ = 0;
  frontend_->clear();
  render();
}

void App::search_navigate(int delta) {
  if (search_results_.empty()) {
    return;
  }

  int n = static_cast<int>(search_results_.size());
  search_current_ = ((search_current_ + delta) % n + n) % n;
  scroll_to_search_hit();
  frontend_->clear();
  render();
}

void App::scroll_to_search_hit() {
  if (search_current_ < 0 || search_current_ >= static_cast<int>(search_results_.size())) {
    return;
  }

  push_jump_history();
  const auto& hit = search_results_[search_current_];
  int page = hit.page;
  page = std::clamp(page, 0, static_cast<int>(layout_.size()) - 1);

  if (view_mode_ == ViewMode::SIDE_BY_SIDE && page % 2 != 0) {
    page = page - 1;
  }

  // Convert hit Y from page points to pixels within the page
  float page_zoom = layout_[hit.page].zoom * user_zoom_;
  int hit_y_px = static_cast<int>(hit.y * page_zoom);

  // Global Y = page top + hit offset within page
  scroll_.y = layout_[hit.page].rect.y + hit_y_px;

  // Clamp scroll
  auto client = frontend_->client_info();
  int vh = client.viewport_pixel().height;
  int total_height = document_height();
  int max_y = std::max(0, total_height - vh);
  scroll_.y = std::clamp(scroll_.y, 0, max_y);

  update_viewport();
}

void App::highlight_page_matches(Pixmap& pixmap, int page_num, float render_zoom) {
  if (search_results_.empty()) {
    return;
  }

  for (int i = 0; i < static_cast<int>(search_results_.size()); ++i) {
    const auto& hit = search_results_[i];
    if (hit.page != page_num) {
      continue;
    }

    PixelRect rect
        = {static_cast<int>(hit.x * render_zoom),
           static_cast<int>(hit.y * render_zoom),
           static_cast<int>(hit.w * render_zoom),
           static_cast<int>(hit.h * render_zoom)};

    if (i == search_current_) {
      pixmap.highlight_rect(rect, colors_.search_active, colors_.search_active_alpha);
    }
    else {
      pixmap.highlight_rect(rect, colors_.search_highlight, colors_.search_highlight_alpha);
    }
  }
}

Theme App::effective_theme() const {
  if (theme_ != Theme::AUTO) {
    return theme_;
  }
  // AUTO: resolve based on detected bg luminance
  if (detected_terminal_bg_) {
    return detected_terminal_bg_->luminance() < 0.5f ? Theme::DARK : Theme::LIGHT;
  }
  // No detection → default to DARK
  return Theme::DARK;
}

std::pair<Color, Color> App::resolve_recolor_colors() const {
  // fg = recolor_dark (replaces black/text)
  Color fg = colors_.recolor_dark;
  if (fg.is_default && detected_terminal_fg_) {
    fg = *detected_terminal_fg_;
  }
  else if (fg.is_default) {
    fg = Color::rgb(192, 192, 192); // Fallback light gray
  }

  // bg = recolor_light (replaces white/background)
  Color bg = colors_.recolor_light;
  if (bg.is_default && detected_terminal_bg_) {
    bg = *detected_terminal_bg_;
  }
  else if (bg.is_default) {
    bg = Color::rgb(30, 30, 30); // Fallback dark
  }

  return {fg, bg};
}

void App::push_jump_history() {
  static constexpr int MAX_JUMP_HISTORY = 100;

  JumpPoint current = {scroll_.x, scroll_.y};

  // Skip duplicate positions
  if (!jump_history_.empty() && jump_index_ < 0) {
    const auto& last = jump_history_.back();
    if (last.scroll_x == current.scroll_x && last.scroll_y == current.scroll_y) {
      return;
    }
  }

  // If in the middle of history, discard forward entries
  if (jump_index_ >= 0) {
    jump_history_.resize(jump_index_ + 1);
    jump_index_ = -1;
  }

  jump_history_.push_back(current);

  // Cap at maximum
  if (static_cast<int>(jump_history_.size()) > MAX_JUMP_HISTORY) {
    jump_history_.erase(jump_history_.begin());
  }
}

void App::enter_link_hints() {
  auto client = frontend_->client_info();
  if (!client.is_valid() || layout_.empty()) {
    return;
  }

  auto vp = client.viewport_pixel();
  int vh = vp.height;
  int vw = vp.width;

  int sidebar_cols = sidebar_effective_width();
  int sidebar_px = 0;
  if (sidebar_cols > 0 && client.cell.width > 0) {
    sidebar_px = sidebar_cols * client.cell.width;
    vw -= sidebar_px;
  }

  // Collect links from visible pages
  link_hints_.clear();
  link_hint_input_.clear();

  for (int p = viewport_first_page_; p <= viewport_last_page_; ++p) {
    auto page_links = doc_.load_links(p);
    for (auto& pl : page_links) {
      // Convert page points → screen cells
      float page_zoom = layout_[p].zoom * user_zoom_;
      int px_x = static_cast<int>(pl.x * page_zoom);
      int px_y = static_cast<int>(pl.y * page_zoom);

      // Global pixel coordinates
      int global_x = layout_[p].rect.x + px_x;
      int global_y = layout_[p].rect.y + px_y;

      // Viewport pixel coordinates
      int vp_x = global_x - scroll_.x + sidebar_px;
      int vp_y = global_y - scroll_.y;

      // Check visibility
      if (vp_y < 0 || vp_y >= vh || vp_x < 0 || vp_x >= vw + sidebar_px) {
        continue;
      }

      // Convert to cell coordinates
      int col = vp_x / client.cell.width;
      int row = vp_y / client.cell.height;

      link_hints_.push_back({std::move(pl), "", col, row});
    }
  }

  if (link_hints_.empty()) {
    last_action_.set("No links on page");
    return;
  }

  // Assign labels
  static const char HINT_CHARS[] = "asdfghjklqwertyuiopzxcvbnm";
  static const int NUM_CHARS = 26;

  if (static_cast<int>(link_hints_.size()) <= NUM_CHARS) {
    for (int i = 0; i < static_cast<int>(link_hints_.size()); ++i) {
      link_hints_[i].label = std::string(1, HINT_CHARS[i]);
    }
  }
  else {
    int idx = 0;
    for (auto& hint : link_hints_) {
      hint.label = std::string(1, HINT_CHARS[idx / NUM_CHARS]) + HINT_CHARS[idx % NUM_CHARS];
      ++idx;
    }
  }

  input_mode_ = InputMode::LINK_HINTS;
  render_link_hints();
}

void App::render_link_hints() {
  std::vector<LinkHintDisplay> displays;
  for (const auto& hint : link_hints_) {
    if (hint.label.substr(0, link_hint_input_.size()) == link_hint_input_) {
      displays.push_back({hint.screen_col, hint.screen_row, hint.label});
    }
  }
  frontend_->show_link_hints(displays);
  update_statusline();
}

void App::follow_link(const PageLink& link) {
  input_mode_ = InputMode::NORMAL;
  link_hints_.clear();
  link_hint_input_.clear();

  if (link.dest_page >= 0) {
    // Internal link — scroll to anchor position
    push_jump_history();
    int page = std::clamp(link.dest_page, 0, static_cast<int>(layout_.size()) - 1);
    float page_zoom = layout_[page].zoom * user_zoom_;
    int dest_y_px = layout_[page].rect.y + static_cast<int>(link.dest_y * page_zoom);
    scroll_.y = dest_y_px;

    // Clamp
    auto client = frontend_->client_info();
    int vh = client.viewport_pixel().height;
    int total_height = document_height();
    int max_y = std::max(0, total_height - vh);
    scroll_.y = std::clamp(scroll_.y, 0, max_y);

    frontend_->clear();
    update_viewport();
  }
  else {
    // External link — copy to clipboard via OSC 52 (works over SSH)
    std::string osc52 = "\x1b]52;c;" + base64::encode(link.uri) + "\x07";
    frontend_->write_raw(osc52.data(), osc52.size());

    // Try to open in default browser (only works locally)
    bool in_ssh = std::getenv("SSH_CLIENT") != nullptr || std::getenv("SSH_TTY") != nullptr;
    if (!in_ssh) {
      pid_t pid = fork();
      if (pid == 0) {
#ifdef __APPLE__
        execlp("open", "open", link.uri.c_str(), nullptr);
#else
        execlp("xdg-open", "xdg-open", link.uri.c_str(), nullptr);
#endif
        _exit(127);
      }
      last_action_.set("Opened: {}", link.uri);
    }
    else {
      last_action_.set("Copied: {}", link.uri);
    }
    update_statusline();
  }
}

void App::exit_link_hints() {
  input_mode_ = InputMode::NORMAL;
  link_hints_.clear();
  link_hint_input_.clear();
  frontend_->clear_overlay();
  update_viewport();
  update_statusline();
}

void App::update_sidebar() {
  if (!sidebar_visible_ || outline_.empty()) {
    return;
  }

  auto client = frontend_->client_info();
  int sidebar_cols = sidebar_effective_width();
  int visible_rows = client.rows - 1; // Exclude status bar
  bool focused = (input_mode_ == InputMode::SIDEBAR);

  if (focused) {
    // Focused mode: show filtered entries with header + filter line
    int header_rows = 2;
    int max_visible = visible_rows - header_rows;

    // Scroll window follows cursor
    if (sidebar_cursor_ < sidebar_scroll_) {
      sidebar_scroll_ = sidebar_cursor_;
    }
    if (sidebar_cursor_ >= sidebar_scroll_ + max_visible) {
      sidebar_scroll_ = sidebar_cursor_ - max_visible + 1;
    }

    std::vector<std::string> lines;
    lines.emplace_back("TOC");

    if (!sidebar_filter_.empty()) {
      lines.push_back(std::format("> {}", sidebar_filter_));
    }
    else {
      lines.emplace_back("");
    }

    int highlight_line = -1;
    int end = std::min(sidebar_scroll_ + max_visible, static_cast<int>(sidebar_filtered_.size()));
    for (int vi = sidebar_scroll_; vi < end; ++vi) {
      const auto& entry = outline_[sidebar_filtered_[vi]];
      std::string indent(entry.level * 2, ' ');
      lines.push_back(std::format("{}{}", indent, entry.title));
      if (vi == sidebar_cursor_) {
        highlight_line = static_cast<int>(lines.size()) - 1;
      }
    }

    if (sidebar_filtered_.empty()) {
      lines.emplace_back("  (no matches)");
    }

    frontend_->show_sidebar(lines, highlight_line, sidebar_cols, true);
  }
  else {
    // Passive mode: show all entries, highlight active section
    int active = active_outline_index();

    // Auto-scroll to keep active entry visible
    if (active >= 0) {
      if (active < sidebar_scroll_) {
        sidebar_scroll_ = active;
      }
      if (active >= sidebar_scroll_ + visible_rows) {
        sidebar_scroll_ = active - visible_rows + 1;
      }
    }

    std::vector<std::string> lines;
    int highlight_line = -1;
    int end = std::min(sidebar_scroll_ + visible_rows, static_cast<int>(outline_.size()));
    for (int i = sidebar_scroll_; i < end; ++i) {
      const auto& entry = outline_[i];
      std::string indent(entry.level * 2, ' ');
      lines.push_back(std::format("{}{}", indent, entry.title));
      if (i == active) {
        highlight_line = static_cast<int>(lines.size()) - 1;
      }
    }

    frontend_->show_sidebar(lines, highlight_line, sidebar_cols, false);
  }
}

ViewState App::view_state() const {
  ViewState vs{
      current_page() + 1,
      doc_.page_count(),
      static_cast<int>(user_zoom_ * 100.0f + 0.5f),
      to_string(view_mode_),
      to_string(theme_),
      search_term_,
      search_current_ >= 0 ? search_current_ + 1 : 0,
      static_cast<int>(search_results_.size()),
      input_mode_ == InputMode::LINK_HINTS,
      {},
      0,
  };

  if (show_stats_) {
    auto [pages, bytes] = cache_stats();
    vs.cache_pages = std::move(pages);
    vs.cache_bytes = bytes;
  }

  return vs;
}
