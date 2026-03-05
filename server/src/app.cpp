#include "app.hpp"

#include "converter.hpp"
#include "util/base64.hpp"
#include "util/stopwatch.hpp"

#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <string>

/// @brief Discrete zoom levels for step-zoom in/out.
static constexpr float ZoomLevels[] = {1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};

std::optional<ViewMode> parse_view_mode(const std::string& s) {
  if (s == "continuous") {
    return ViewMode::Continuous;
  }
  if (s == "page" || s == "page-width") {
    return ViewMode::PageWidth;
  }
  if (s == "page-height") {
    return ViewMode::PageHeight;
  }
  if (s == "side-by-side") {
    return ViewMode::SideBySide;
  }
  return std::nullopt;
}

const char* to_label(Theme t, Theme effective) {
  switch (t) {
    case Theme::Light:
      return "Light";
    case Theme::Dark:
      return "Dark";
    case Theme::Auto:
      return (effective == Theme::Dark) ? "Auto/Dark" : "Auto/Light";
    case Theme::Terminal:
      return "Terminal";
  }
  return "";
}

std::optional<Theme> parse_theme(const std::string& s) {
  if (s == "dark") {
    return Theme::Dark;
  }
  if (s == "light") {
    return Theme::Light;
  }
  if (s == "auto") {
    return Theme::Auto;
  }
  if (s == "terminal") {
    return Theme::Terminal;
  }
  return std::nullopt;
}

App::App(std::unique_ptr<Frontend> frontend, const Args& args, std::optional<Color> detected_fg, std::optional<Color> detected_bg)
    : frontend_(std::move(frontend))
    , doc_(args.file)
    , colors_(args.colors)
    , bindings_(args.key_bindings)
    , detected_terminal_fg_(detected_fg)
    , detected_terminal_bg_(detected_bg)
    , show_stats_(args.show_stats)
    , scroll_lines_(args.scroll_lines)
    , max_page_cache_(args.max_page_cache)
    , render_scale_setting_(args.render_scale)
    , file_path_(args.file)
    , source_path_(args.source_file)
    , converter_cmd_(args.source_file.empty() ? "" : args.converter) {
  if (auto vm = parse_view_mode(args.view_mode)) {
    view_mode_ = *vm;
  }
  if (auto th = parse_theme(args.theme)) {
    theme_ = *th;
  }
  if (args.watch) {
    watch_ = true;
    const std::string& watched = source_path_.empty() ? file_path_ : source_path_;
    struct stat st;
    if (stat(watched.c_str(), &st) == 0) {
      watched_mtime_ = st.st_mtime;
    }
  }
  spdlog::info("opened document: {} pages, mode: {}, theme: {}", doc_.page_count(), args.view_mode, args.theme);
}

InputMode App::input_mode() const {
  switch (app_mode_) {
    case AppMode::Visual:
      return InputMode::Visual;
    case AppMode::VisualBlock:
      return InputMode::VisualBlock;
    case AppMode::LinkHints:
      return InputMode::LinkHints;
    default:
      return InputMode::Normal;
  }
}

void App::build_layout() {
  auto client = frontend_->client_info();
  layout_size_ = client.pixel;
  int num_pages = doc_.page_count();
  auto vp = client.viewport_pixel();
  int vh = vp.height;
  int vw = vp.width;

  spdlog::debug("build_layout: pixel={} vp={} cell={} pages={} zoom={:.2f}", client.pixel, vp, client.cell, num_pages, user_zoom_);
  layout_.resize(num_pages);

  if (view_mode_ == ViewMode::PageHeight) {
    for (int i = 0; i < num_pages; ++i) {
      auto ps = doc_.page_size(i);
      float zoom = std::min(static_cast<float>(vw) / ps.width, static_cast<float>(vh) / ps.height);
      int rendered_w = static_cast<int>(ps.width * zoom);
      int slot_h = std::max(vh, static_cast<int>(ps.height * zoom * user_zoom_));
      layout_[i] = {{0, i * slot_h, rendered_w, slot_h}, zoom};
    }
  }
  else if (view_mode_ == ViewMode::SideBySide) {
    int half_w = vw / 2;
    int y = 0;
    for (int i = 0; i < num_pages; ++i) {
      auto ps = doc_.page_size(i);
      bool is_last_odd = (i == num_pages - 1) && (i % 2 == 0);
      int fit_w = is_last_odd ? vw : half_w;
      float zoom = std::min(static_cast<float>(fit_w) / ps.width, static_cast<float>(vh) / ps.height);
      int rendered_w = static_cast<int>(ps.width * zoom);
      int slot_h = std::max(vh, static_cast<int>(ps.height * zoom * user_zoom_));
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
    if (view_mode_ == ViewMode::Continuous && cell_h > 0) {
      gap = std::max(1, (PageGapPx + cell_h - 1) / cell_h) * cell_h;
    }
    int y = 0;
    for (int i = 0; i < num_pages; ++i) {
      auto ps = doc_.page_size(i);
      float zoom = static_cast<float>(vw) / ps.width;
      int h = static_cast<int>(ps.height * zoom * user_zoom_);
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

  if (setting == RenderScale::Auto) {
    setting = frontend_->supports_image_viewporting() ? RenderScale::X05 : RenderScale::Never;
  }

  if (setting == RenderScale::Never) {
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

  float dynamic_scale = std::pow(2.0f, std::floor(std::log2(user_zoom_ * 0.5f)));

  return std::max(floor_scale, dynamic_scale);
}

void App::handle_zoom_change(float old_zoom) {
  auto client = frontend_->client_info();
  int vw = client.viewport_pixel().width;
  int vh = client.viewport_pixel().height;

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
    if (eff == Theme::Dark) {
      pixmap.invert();
    }
    else if (eff == Theme::Terminal) {
      auto [fg, bg] = resolve_recolor_colors();
      pixmap.recolor(fg, bg, colors_.recolor_accent);
    }

    // Save post-theme, pre-highlight pixels for fast selection refresh
    auto base = pixmap.pack_pixels();
    int base_w = pixmap.width();
    int base_h = pixmap.height();
    int base_comp = pixmap.components();

    highlight_page_matches(pixmap, i, render_zoom);
    highlight_selection(pixmap, i, render_zoom);

    // Grid dimensions based on display size (not rendered pixel size)
    // so images display at correct width in tmux unicode placeholder mode.
    float display_zoom = base_zoom * user_zoom_;
    auto ps = doc_.page_size(i);
    int display_w = static_cast<int>(ps.width * display_zoom);
    int display_h = static_cast<int>(ps.height * display_zoom);
    int cols = (display_w + client.cell.width - 1) / client.cell.width;
    int rows = (display_h + client.cell.height - 1) / client.cell.height;

    uint32_t image_id = frontend_->upload_image(pixmap, cols, rows);
    size_t mem = static_cast<size_t>(pixmap.width()) * pixmap.height() * pixmap.components();
    page_cache_[i] = {image_id, {pixmap.width(), pixmap.height()}, {cols, rows}, rs, render_zoom, mem, std::move(base), base_w, base_h, base_comp};
  }
}

void App::evict_distant_pages(int first, int last) {
  int keep_lo = std::max(0, first - 2);
  int keep_hi = std::min(static_cast<int>(layout_.size()) - 1, last + 2);

  // Distance-based eviction: remove pages far from viewport
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

  // Size-based eviction: if still over budget, evict most distant pages
  if (max_page_cache_ == 0) {
    return;
  }
  size_t total = 0;
  for (const auto& [page, cached] : page_cache_) {
    total += cached.memory_bytes;
  }
  if (total <= max_page_cache_) {
    return;
  }

  int mid = (first + last) / 2;
  std::vector<int> pages_by_distance;
  pages_by_distance.reserve(page_cache_.size());
  for (const auto& [page, cached] : page_cache_) {
    pages_by_distance.push_back(page);
  }
  std::sort(pages_by_distance.begin(), pages_by_distance.end(), [mid](int a, int b) { return std::abs(a - mid) > std::abs(b - mid); });

  for (int page : pages_by_distance) {
    if (total <= max_page_cache_) {
      break;
    }
    // Never evict visible pages
    if (page >= first && page <= last) {
      continue;
    }
    total -= page_cache_[page].memory_bytes;
    frontend_->free_image(page_cache_[page].image_id);
    page_cache_.erase(page);
    spdlog::debug("evicted page {} (cache over budget)", page);
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

void App::update_viewport() {
  auto client = frontend_->client_info();
  if (!client.is_valid() || layout_.empty()) {
    spdlog::debug("update_viewport: bail (valid={}, layout={})", client.is_valid(), layout_.size());
    return;
  }

  auto vp = client.viewport_pixel();
  int vh = vp.height;
  int vw = vp.width;

  int num_pages = static_cast<int>(layout_.size());
  spdlog::debug("update_viewport: scroll={} vp={} cell={}", scroll_, vp, client.cell);

  // Find visible pages
  int first = page_at_y(scroll_.y);
  int last = first;

  // In SIDE_BY_SIDE, snap back to pair start
  if (view_mode_ == ViewMode::SideBySide && first > 0 && layout_[first - 1].rect.y == layout_[first].rect.y) {
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
    auto ps = doc_.page_size(i);
    int zoomed_w = static_cast<int>(ps.width * lay.zoom * user_zoom_);
    int zoomed_h = static_cast<int>(ps.height * lay.zoom * user_zoom_);

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
    int dst_col = 0;

    if (view_mode_ == ViewMode::PageHeight) {
      int vert_pad = std::max(0, (lay.rect.height - zoomed_h) / 2);
      content_top = page_top + vert_pad;
      if (zoomed_w < vw) {
        dst_col = (vw - zoomed_w) / 2 / client.cell.width;
      }
    }
    else if (view_mode_ == ViewMode::SideBySide) {
      int vert_pad = std::max(0, (lay.rect.height - zoomed_h) / 2);
      content_top = page_top + vert_pad;
      bool is_last_odd = (i == num_pages - 1) && (i % 2 == 0);
      if (is_last_odd) {
        if (zoomed_w < vw) {
          dst_col = (vw - zoomed_w) / 2 / client.cell.width;
        }
      }
      else if (i % 2 == 0) {
        int half_w = vw / 2;
        if (zoomed_w < half_w) {
          dst_col = (half_w - zoomed_w) / 2 / client.cell.width;
        }
      }
      else {
        int half_w = vw / 2;
        if (zoomed_w < half_w) {
          dst_col = (half_w + (half_w - zoomed_w) / 2) / client.cell.width;
        }
        else {
          dst_col = half_w / client.cell.width;
        }
      }
    }
    else {
      // CONTINUOUS / PAGE_WIDTH: center when zoomed content is narrower than viewport
      if (zoomed_w < vw) {
        dst_col = (vw - zoomed_w) / 2 / client.cell.width;
      }
    }

    // Clamp to viewport
    PixelRect content_rect = {0, content_top, zoomed_w, zoomed_h};
    PixelRect viewport_rect = {0, 0, vw, vh};
    auto visible = content_rect.intersect(viewport_rect);
    if (visible.height <= 0) {
      continue;
    }

    // Source rect in image pixel coordinates, clamped to image bounds
    int clamped_scroll_x = std::clamp(scroll_.x, 0, std::max(0, zoomed_w - vw));
    PixelRect image_bounds = {0, 0, cached.pixel_size.width, cached.pixel_size.height};
    PixelRect src = PixelRect::from_xywh(
                        static_cast<int>(clamped_scroll_x * img_scale),
                        static_cast<int>((visible.top() - content_top) * img_scale),
                        static_cast<int>(std::min(zoomed_w, vw) * img_scale),
                        static_cast<int>(visible.height * img_scale)
    )
                        .intersect(image_bounds);

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

  int total_height = document_height();
  int max_y = std::max(0, total_height - vh);

  int new_y = std::clamp(scroll_.y + dy, 0, max_y);

  // In non-continuous modes, clamp scroll to stay within the current page
  if (view_mode_ != ViewMode::Continuous) {
    int p = page_at_y(new_y);
    int page_top = layout_[p].rect.y;
    int page_max = page_top + std::max(0, layout_[p].rect.height - vh);
    new_y = std::clamp(new_y, page_top, page_max);
  }

  // Clamp horizontal scroll based on zoomed page width
  int p = page_at_y(new_y);
  int zoomed_w = static_cast<int>(doc_.page_size(p).width * layout_[p].zoom * user_zoom_);
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

    int total_height = document_height();
    int max_y = std::max(0, total_height - vh);
    scroll_.y = std::clamp(scroll_.y, 0, max_y);

    if (user_zoom_ <= 1.0f) {
      scroll_.x = 0;
    }
    else {
      int p = page_at_y(scroll_.y);
      int zoomed_w = static_cast<int>(doc_.page_size(p).width * layout_[p].zoom * user_zoom_);
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
  if (view_mode_ == ViewMode::SideBySide && page % 2 != 0) {
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
    }
  }

  if (watch_) {
    const std::string& watched = source_path_.empty() ? file_path_ : source_path_;
    struct stat st;
    if (stat(watched.c_str(), &st) == 0 && st.st_mtime != watched_mtime_) {
      watched_mtime_ = st.st_mtime;
      if (!source_path_.empty()) {
        try {
          reconvert(source_path_, file_path_, converter_cmd_);
        }
        catch (const std::runtime_error& e) {
          spdlog::error("watch: reconversion failed: {}", e.what());
          last_action_.set("Reconversion failed");
          return;
        }
      }
      spdlog::info("watch: file changed, reloading");
      do_reload();
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

void App::handle_command(const Command& cmd) {
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
            if (view_mode_ == ViewMode::SideBySide) {
              jump_to_page(p + 2);
            }
            else if (view_mode_ != ViewMode::Continuous) {
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
            if (view_mode_ == ViewMode::SideBySide) {
              jump_to_page(p - 2);
            }
            else if (view_mode_ != ViewMode::Continuous) {
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
          float old = user_zoom_;
          for (float lvl : ZoomLevels) {
            if (lvl > user_zoom_ + 0.01f) {
              user_zoom_ = lvl;
              break;
            }
          }
          spdlog::info("zoom in: {:.0f}%", user_zoom_ * 100.0f);
          handle_zoom_change(old);
        }
        else if constexpr (std::is_same_v<T, cmd::ZoomOut>) {
          float old = user_zoom_;
          float best = ZoomLevels[0];
          for (float lvl : ZoomLevels) {
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
            case ViewMode::Continuous:
              view_mode_ = ViewMode::PageWidth;
              break;
            case ViewMode::PageWidth:
              view_mode_ = ViewMode::PageHeight;
              break;
            case ViewMode::PageHeight:
              view_mode_ = ViewMode::SideBySide;
              break;
            case ViewMode::SideBySide:
              view_mode_ = ViewMode::Continuous;
              break;
          }
          user_zoom_ = 1.0f;
          scroll_.x = 0;
          render();
        }
        else if constexpr (std::is_same_v<T, cmd::SetViewMode>) {
          apply_view_mode(arg.mode);
        }
        else if constexpr (std::is_same_v<T, cmd::ToggleTheme>) {
          if (theme_ == Theme::Dark) {
            theme_ = Theme::Light;
          }
          else if (theme_ == Theme::Light) {
            theme_ = Theme::Terminal;
          }
          else {
            theme_ = Theme::Dark;
          }
          frontend_->clear();
          render();
        }
        else if constexpr (std::is_same_v<T, cmd::SetTheme>) {
          apply_theme(arg.theme);
        }
        else if constexpr (std::is_same_v<T, cmd::SetRenderScale>) {
          apply_render_scale(arg.strategy);
        }
        else if constexpr (std::is_same_v<T, cmd::SetSidebarWidth>) {
        }
        else if constexpr (std::is_same_v<T, cmd::Reload>) {
          do_reload();
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
          if (app_mode_ == AppMode::LinkHints) {
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
          if (app_mode_ == AppMode::LinkHints) {
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
        }
        else if constexpr (std::is_same_v<T, cmd::ToggleSidebar>) {
        }
        else if constexpr (std::is_same_v<T, cmd::EnterCommandMode>) {
        }
        else if constexpr (std::is_same_v<T, cmd::EnterSearchMode>) {
        }
        else if constexpr (std::is_same_v<T, cmd::ShowHelp>) {
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
          auto pp = screen_to_page_point(arg.col, arg.row);
          if (pp.page < 0) {
            return;
          }

          // Hit-test against page links
          auto page_links = doc_.load_links(pp.page);
          for (auto& pl : page_links) {
            if (pl.rect.contains(pp.pos)) {
              follow_link(pl);
              return;
            }
          }

          // Store click point for potential drag-to-select
          last_click_point_ = pp;
          has_click_point_ = true;
        }
        else if constexpr (std::is_same_v<T, cmd::EnterVisualMode>) {
          enter_visual_mode(false);
        }
        else if constexpr (std::is_same_v<T, cmd::EnterVisualBlockMode>) {
          enter_visual_mode(true);
        }
        else if constexpr (std::is_same_v<T, cmd::SelectionMove>) {
          move_selection_extent(arg.dx, arg.dy);
        }
        else if constexpr (std::is_same_v<T, cmd::SelectionMoveWord>) {
          move_selection_word(arg.direction);
        }
        else if constexpr (std::is_same_v<T, cmd::SelectionGoto>) {
          selection_goto(arg.target);
        }
        else if constexpr (std::is_same_v<T, cmd::SelectionYank>) {
          yank_selection();
        }
        else if constexpr (std::is_same_v<T, cmd::SelectionCancel>) {
          cancel_selection();
        }
        else if constexpr (std::is_same_v<T, cmd::DragStart>) {
          last_click_point_ = screen_to_page_point(arg.col, arg.row);
          has_click_point_ = (last_click_point_.page >= 0);
        }
        else if constexpr (std::is_same_v<T, cmd::DragUpdate>) {
          if (app_mode_ == AppMode::Normal && has_click_point_) {
            selection_anchor_ = last_click_point_;
            selection_extent_ = last_click_point_;
            app_mode_ = AppMode::Visual;
          }
          if (app_mode_ == AppMode::Visual || app_mode_ == AppMode::VisualBlock) {
            update_selection_extent(arg.col, arg.row);
          }
        }
        else if constexpr (std::is_same_v<T, cmd::DragEnd>) {
          if (app_mode_ == AppMode::Visual || app_mode_ == AppMode::VisualBlock) {
            update_selection_extent(arg.col, arg.row);
          }
        }
      },
      cmd
  );
}

void App::do_reload() {
  try {
    for (auto& [page, cached] : page_cache_) {
      frontend_->free_image(cached.image_id);
    }
    page_cache_.clear();

    doc_.reload(file_path_);

    outline_.clear();
    search_results_.clear();
    search_term_.clear();
    search_current_ = -1;

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

void App::apply_theme(const std::string& name) {
  auto th = parse_theme(name);
  if (th) {
    theme_ = *th;
    frontend_->clear();
    render();
  }
  else {
    last_action_.set("Unknown theme: {}", name);
  }
}

void App::apply_view_mode(const std::string& name) {
  auto vm = parse_view_mode(name);
  if (!vm) {
    last_action_.set("Unknown mode: {}", name);
    return;
  }
  view_mode_ = *vm;
  user_zoom_ = 1.0f;
  scroll_.x = 0;
  render();
}

void App::apply_render_scale(const std::string& name) {
  if (name == "auto") {
    render_scale_setting_ = RenderScale::Auto;
  }
  else if (name == "never") {
    render_scale_setting_ = RenderScale::Never;
  }
  else if (name == "0.25") {
    render_scale_setting_ = RenderScale::X025;
  }
  else if (name == "0.5") {
    render_scale_setting_ = RenderScale::X05;
  }
  else if (name == "1") {
    render_scale_setting_ = RenderScale::X1;
  }
  else if (name == "2") {
    render_scale_setting_ = RenderScale::X2;
  }
  else if (name == "4") {
    render_scale_setting_ = RenderScale::X4;
  }
  else {
    last_action_.set("Unknown render-scale: {}", name);
    return;
  }
  frontend_->clear();
  render();
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
    int hit_global_y = layout_[hit.page].rect.y + static_cast<int>(hit.rect.y * page_zoom);
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

  if (view_mode_ == ViewMode::SideBySide && page % 2 != 0) {
    page = page - 1;
  }

  // Convert hit Y from page points to pixels within the page
  float page_zoom = layout_[hit.page].zoom * user_zoom_;
  int hit_y_px = static_cast<int>(hit.rect.y * page_zoom);

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
        = {static_cast<int>(hit.rect.x * render_zoom),
           static_cast<int>(hit.rect.y * render_zoom),
           static_cast<int>(hit.rect.width * render_zoom),
           static_cast<int>(hit.rect.height * render_zoom)};

    if (i == search_current_) {
      pixmap.highlight_rect(rect, colors_.search_active, colors_.search_active_alpha);
    }
    else {
      pixmap.highlight_rect(rect, colors_.search_highlight, colors_.search_highlight_alpha);
    }
  }
}

Theme App::effective_theme() const {
  if (theme_ != Theme::Auto) {
    return theme_;
  }
  // AUTO: resolve based on detected bg luminance
  if (detected_terminal_bg_) {
    return detected_terminal_bg_->luminance() < 0.5f ? Theme::Dark : Theme::Light;
  }
  // No detection → default to DARK
  return Theme::Dark;
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
  static constexpr int MaxJumpHistory = 100;

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
  if (static_cast<int>(jump_history_.size()) > MaxJumpHistory) {
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

  // Collect links from visible pages
  link_hints_.clear();
  link_hint_input_.clear();

  for (int p = viewport_first_page_; p <= viewport_last_page_; ++p) {
    auto page_links = doc_.load_links(p);
    for (auto& pl : page_links) {
      // Convert page points → screen cells
      float page_zoom = layout_[p].zoom * user_zoom_;
      int px_x = static_cast<int>(pl.rect.x * page_zoom);
      int px_y = static_cast<int>(pl.rect.y * page_zoom);

      // Global pixel coordinates
      int global_x = layout_[p].rect.x + px_x;
      int global_y = layout_[p].rect.y + px_y;

      // Viewport pixel coordinates
      int vp_x = global_x - scroll_.x;
      int vp_y = global_y - scroll_.y;

      // Check visibility
      if (vp_y < 0 || vp_y >= vh || vp_x < 0 || vp_x >= vw) {
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
  static const char HintChars[] = "asdfghjklqwertyuiopzxcvbnm";
  static const int NumChars = 26;

  if (static_cast<int>(link_hints_.size()) <= NumChars) {
    for (int i = 0; i < static_cast<int>(link_hints_.size()); ++i) {
      link_hints_[i].label = std::string(1, HintChars[i]);
    }
  }
  else {
    int idx = 0;
    for (auto& hint : link_hints_) {
      hint.label = std::string(1, HintChars[idx / NumChars]) + HintChars[idx % NumChars];
      ++idx;
    }
  }

  app_mode_ = AppMode::LinkHints;
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
}

void App::follow_link(const PageLink& link) {
  app_mode_ = AppMode::Normal;
  link_hints_.clear();
  link_hint_input_.clear();

  if (link.dest_page >= 0) {
    // Internal link — scroll to anchor position
    push_jump_history();
    int page = std::clamp(link.dest_page, 0, static_cast<int>(layout_.size()) - 1);
    float page_zoom = layout_[page].zoom * user_zoom_;
    int dest_y_px = layout_[page].rect.y + static_cast<int>(link.dest.y * page_zoom);
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
  }
}

void App::exit_link_hints() {
  app_mode_ = AppMode::Normal;
  link_hints_.clear();
  link_hint_input_.clear();
  frontend_->clear_overlay();
  update_viewport();
}

PagePoint App::screen_to_page_point(int col, int row) const {
  auto client = frontend_->client_info();
  if (!client.is_valid() || layout_.empty() || client.cell.width == 0 || client.cell.height == 0) {
    return {-1, {}};
  }

  int vp_x = col * client.cell.width;
  int vp_y = row * client.cell.height;
  int global_x = vp_x + scroll_.x;
  int global_y = vp_y + scroll_.y;

  int p = page_at_y(global_y);
  if (p < 0 || p >= static_cast<int>(layout_.size())) {
    return {-1, {}};
  }

  float page_zoom = layout_[p].zoom * user_zoom_;
  if (page_zoom <= 0.0f) {
    return {-1, {}};
  }
  float pt_x = static_cast<float>(global_x - layout_[p].rect.x) / page_zoom;
  float pt_y = static_cast<float>(global_y - layout_[p].rect.y) / page_zoom;
  return {p, {pt_x, pt_y}};
}

void App::enter_visual_mode(bool block_mode) {
  AppMode new_app_mode = block_mode ? AppMode::VisualBlock : AppMode::Visual;

  if (app_mode_ == AppMode::Visual || app_mode_ == AppMode::VisualBlock) {
    app_mode_ = new_app_mode;
    refresh_selection_pages();
    update_viewport();
    return;
  }

  // Enter visual mode with anchor at center of viewport
  auto client = frontend_->client_info();
  auto pp = screen_to_page_point(client.cols / 2, client.rows / 2);
  if (pp.page < 0) {
    return;
  }

  selection_anchor_ = pp;
  selection_extent_ = pp;
  app_mode_ = new_app_mode;
  refresh_selection_pages();
  update_viewport();
}

void App::update_selection_extent(int col, int row) {
  auto pp = screen_to_page_point(col, row);
  if (pp.page < 0) {
    return;
  }

  selection_extent_ = pp;
  refresh_selection_pages();
  update_viewport();
}

void App::move_selection_extent(int dx_cells, int dy_cells) {
  auto client = frontend_->client_info();
  if (!client.is_valid() || layout_.empty() || client.cell.width == 0 || client.cell.height == 0) {
    return;
  }

  // Convert current extent to approximate screen cell
  int p = selection_extent_.page;
  if (p < 0 || p >= static_cast<int>(layout_.size())) {
    return;
  }

  float page_zoom = layout_[p].zoom * user_zoom_;

  int global_x = static_cast<int>(selection_extent_.pos.x * page_zoom) + layout_[p].rect.x;
  int global_y = static_cast<int>(selection_extent_.pos.y * page_zoom) + layout_[p].rect.y;
  int vp_x = global_x - scroll_.x;
  int vp_y = global_y - scroll_.y;

  int col = vp_x / client.cell.width + dx_cells;
  int row = vp_y / client.cell.height + dy_cells;

  // Auto-scroll if cursor moves beyond viewport
  int max_row = client.rows - 2; // -1 for statusline
  if (row < 0) {
    scroll(0, row * client.cell.height);
    row = 0;
  }
  else if (row > max_row) {
    scroll(0, (row - max_row) * client.cell.height);
    row = max_row;
  }

  auto pp = screen_to_page_point(col, row);
  if (pp.page < 0) {
    return;
  }

  selection_extent_ = pp;
  refresh_selection_pages();
  update_viewport();
}

void App::move_selection_word(int direction) {
  if (app_mode_ != AppMode::Visual && app_mode_ != AppMode::VisualBlock) {
    return;
  }

  PagePoint target;
  if (direction > 0) {
    target = doc_.next_word_boundary(selection_extent_);
  }
  else {
    target = doc_.prev_word_boundary(selection_extent_);
  }

  if (target.page == selection_extent_.page && target.pos == selection_extent_.pos) {
    return;
  }

  selection_extent_ = target;

  // Auto-scroll to keep the extent visible
  if (target.page >= 0 && target.page < static_cast<int>(layout_.size())) {
    float page_zoom = layout_[target.page].zoom * user_zoom_;
    int global_y = static_cast<int>(target.pos.y * page_zoom) + layout_[target.page].rect.y;
    auto client = frontend_->client_info();
    int vh = client.viewport_pixel().height;
    if (global_y < scroll_.y) {
      scroll_.y = global_y;
    }
    else if (global_y > scroll_.y + vh - client.cell.height * 2) {
      scroll_.y = global_y - vh + client.cell.height * 2;
    }
  }

  refresh_selection_pages();
  update_viewport();
}

void App::selection_goto(cmd::SelectionTarget target) {
  if (app_mode_ != AppMode::Visual && app_mode_ != AppMode::VisualBlock) {
    return;
  }

  PagePoint dest;
  switch (target) {
    case cmd::SelectionTarget::WordEnd:
      dest = doc_.end_of_word_boundary(selection_extent_);
      break;
    case cmd::SelectionTarget::LineStart:
      dest = doc_.line_start(selection_extent_);
      break;
    case cmd::SelectionTarget::LineEnd:
      dest = doc_.line_end(selection_extent_);
      break;
    case cmd::SelectionTarget::FirstNonSpace:
      dest = doc_.first_non_space(selection_extent_);
      break;
    case cmd::SelectionTarget::DocStart: {
      // Go to first char on page 0
      auto chars_first = doc_.line_start({0, {}});
      dest = chars_first;
      break;
    }
    case cmd::SelectionTarget::DocEnd: {
      // Go to last char on last page
      int last_page = doc_.page_count() - 1;
      if (last_page >= 0) {
        auto ps = doc_.page_size(last_page);
        dest = doc_.line_end({last_page, {ps.width, ps.height}});
      }
      else {
        return;
      }
      break;
    }
  }

  if (dest.page == selection_extent_.page && dest.pos == selection_extent_.pos) {
    return;
  }

  selection_extent_ = dest;

  // Auto-scroll to keep the extent visible
  if (dest.page >= 0 && dest.page < static_cast<int>(layout_.size())) {
    float page_zoom = layout_[dest.page].zoom * user_zoom_;
    int global_y = static_cast<int>(dest.pos.y * page_zoom) + layout_[dest.page].rect.y;
    auto client = frontend_->client_info();
    int vh = client.viewport_pixel().height;
    if (global_y < scroll_.y) {
      scroll_.y = global_y;
    }
    else if (global_y > scroll_.y + vh - client.cell.height * 2) {
      scroll_.y = global_y - vh + client.cell.height * 2;
    }
  }

  refresh_selection_pages();
  update_viewport();
}

void App::invalidate_selection_pages() {
  int lo = std::min(selection_anchor_.page, selection_extent_.page);
  int hi = std::max(selection_anchor_.page, selection_extent_.page);
  for (int p = lo; p <= hi; ++p) {
    auto it = page_cache_.find(p);
    if (it != page_cache_.end()) {
      frontend_->free_image(it->second.image_id);
      page_cache_.erase(it);
    }
  }
}

void App::refresh_selection_pages() {
  // Determine which pages are affected by the selection
  int lo = std::min(selection_anchor_.page, selection_extent_.page);
  int hi = std::max(selection_anchor_.page, selection_extent_.page);
  lo = std::max(lo, 0);
  hi = std::min(hi, static_cast<int>(layout_.size()) - 1);

  auto client = frontend_->client_info();
  if (!client.is_valid()) {
    return;
  }

  for (int p = lo; p <= hi; ++p) {
    auto it = page_cache_.find(p);
    if (it == page_cache_.end() || it->second.base_pixels.empty()) {
      continue;
    }

    auto& cached = it->second;

    // Reconstruct pixmap from cached base pixels
    Pixmap pixmap = Pixmap::from_pixels(doc_.ctx(), cached.base_w, cached.base_h, cached.base_comp, cached.base_pixels.data());

    // Re-apply highlights on the fresh copy
    highlight_page_matches(pixmap, p, cached.render_zoom);
    highlight_selection(pixmap, p, cached.render_zoom);

    // Re-upload
    frontend_->free_image(cached.image_id);
    cached.image_id = frontend_->upload_image(pixmap, cached.cell_grid.width, cached.cell_grid.height);
  }
}

void App::highlight_selection(Pixmap& pixmap, int page_num, float render_zoom) {
  if (app_mode_ != AppMode::Visual && app_mode_ != AppMode::VisualBlock) {
    return;
  }

  std::vector<SearchHit> quads;
  if (app_mode_ == AppMode::Visual) {
    quads = doc_.selection_quads(page_num, selection_anchor_, selection_extent_);
  }
  else {
    // VISUAL_BLOCK: compute rect from anchor/extent
    float x0 = std::min(selection_anchor_.pos.x, selection_extent_.pos.x);
    float y0 = std::min(selection_anchor_.pos.y, selection_extent_.pos.y);
    float x1 = std::max(selection_anchor_.pos.x, selection_extent_.pos.x);
    float y1 = std::max(selection_anchor_.pos.y, selection_extent_.pos.y);

    // For block mode, only highlight on pages in range
    int lo = std::min(selection_anchor_.page, selection_extent_.page);
    int hi = std::max(selection_anchor_.page, selection_extent_.page);
    if (page_num < lo || page_num > hi) {
      return;
    }

    quads = doc_.rect_selection_quads(page_num, x0, y0, x1, y1);
  }

  for (const auto& hit : quads) {
    PixelRect rect
        = {static_cast<int>(hit.rect.x * render_zoom),
           static_cast<int>(hit.rect.y * render_zoom),
           static_cast<int>(hit.rect.width * render_zoom),
           static_cast<int>(hit.rect.height * render_zoom)};
    pixmap.highlight_rect(rect, colors_.selection_highlight, colors_.selection_highlight_alpha);
  }
}

void App::yank_selection() {
  std::string text;
  if (app_mode_ == AppMode::Visual) {
    text = doc_.copy_text(selection_anchor_, selection_extent_);
  }
  else if (app_mode_ == AppMode::VisualBlock) {
    int lo = std::min(selection_anchor_.page, selection_extent_.page);
    int hi = std::max(selection_anchor_.page, selection_extent_.page);
    float x0 = std::min(selection_anchor_.pos.x, selection_extent_.pos.x);
    float y0 = std::min(selection_anchor_.pos.y, selection_extent_.pos.y);
    float x1 = std::max(selection_anchor_.pos.x, selection_extent_.pos.x);
    float y1 = std::max(selection_anchor_.pos.y, selection_extent_.pos.y);
    for (int p = lo; p <= hi; ++p) {
      auto page_text = doc_.copy_rect_text(p, x0, y0, x1, y1);
      if (!page_text.empty()) {
        if (!text.empty()) {
          text += '\n';
        }
        text += page_text;
      }
    }
  }

  copy_to_clipboard(text);
  last_action_.set("Yanked {} chars", text.size());
  cancel_selection();
}

void App::cancel_selection() {
  app_mode_ = AppMode::Normal;
  has_click_point_ = false;
  refresh_selection_pages();
  update_viewport();
}

void App::copy_to_clipboard(const std::string& text) {
  if (text.empty()) {
    return;
  }

  // OSC 52 clipboard (works over SSH)
  std::string osc52 = "\x1b]52;c;" + base64::encode(text) + "\x07";
  frontend_->write_raw(osc52.data(), osc52.size());

  // System clipboard fallback
  pid_t pid = fork();
  if (pid == 0) {
#ifdef __APPLE__
    FILE* pipe = popen("pbcopy", "w");
#elif defined(__linux__)
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    FILE* pipe = popen(wayland ? "wl-copy" : "xclip -selection clipboard", "w");
#else
    FILE* pipe = nullptr;
#endif
    if (pipe) {
      std::fwrite(text.data(), 1, text.size(), pipe);
      pclose(pipe);
    }
    _exit(0);
  }
}

ViewState App::view_state() const {
  std::string vis_mode;
  if (app_mode_ == AppMode::Visual) {
    vis_mode = "visual";
  }
  else if (app_mode_ == AppMode::VisualBlock) {
    vis_mode = "visual-block";
  }

  ViewState vs{
      current_page() + 1,
      doc_.page_count(),
      static_cast<int>(user_zoom_ * 100.0f + 0.5f),
      to_string(view_mode_),
      to_string(theme_),
      search_term_,
      search_current_ >= 0 ? search_current_ + 1 : 0,
      static_cast<int>(search_results_.size()),
      app_mode_ == AppMode::LinkHints,
      std::move(vis_mode),
      {},
      0,
      {},
  };

  if (show_stats_) {
    auto [pages, bytes] = cache_stats();
    vs.cache_pages = std::move(pages);
    vs.cache_bytes = bytes;
  }

  if (last_action_.active()) {
    vs.flash_message = last_action_.text();
  }

  return vs;
}
