#include "app.h"

#include "input_event.h"
#include "stopwatch.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <string>

App::App(std::unique_ptr<Frontend> frontend, const Args& args)
    : frontend_(std::move(frontend))
    , doc_(args.file)
    , oversample_setting_(args.oversample) {
  if (args.view_mode == "page") {
    view_mode_ = ViewMode::PAGE_WIDTH;
  }
  else if (args.view_mode == "page-height") {
    view_mode_ = ViewMode::PAGE_HEIGHT;
  }
  else if (args.view_mode == "side-by-side") {
    view_mode_ = ViewMode::SIDE_BY_SIDE;
  }
  spdlog::info("opened document: {} pages, mode: {}", doc_.page_count(), args.view_mode);
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

int App::effective_oversample() const {
  Oversample setting = oversample_setting_;

  if (setting == Oversample::AUTO) {
    setting = frontend_->supports_image_viewporting() ? Oversample::X1 : Oversample::NEVER;
  }

  if (setting == Oversample::NEVER) {
    return 0;
  }

  int floor_os = 1;
  if (setting == Oversample::X2) {
    floor_os = 2;
  }
  else if (setting == Oversample::X4) {
    floor_os = 4;
  }

  int dynamic_os = 1;
  if (user_zoom_ > 1.0f) {
    dynamic_os = 2;
  }
  if (user_zoom_ > 2.0f) {
    dynamic_os = 4;
  }
  if (user_zoom_ > 4.0f) {
    dynamic_os = 8;
  }

  return std::max(floor_os, dynamic_os);
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

  int os = effective_oversample();

  for (int i = first; i <= last; ++i) {
    float base_zoom = layout_[i].zoom;

    float render_zoom = (os == 0) ? base_zoom * user_zoom_ : base_zoom * os;

    auto it = page_cache_.find(i);
    if (it != page_cache_.end()) {
      if (os == 0) {
        // NEVER mode: re-render only if zoom changed
        if (it->second.render_zoom == render_zoom) {
          continue;
        }
        frontend_->free_image(it->second.image_id);
        page_cache_.erase(it);
      }
      else {
        // Oversample mode: keep if cached oversample is sufficient
        if (it->second.oversample >= os) {
          continue;
        }
        // Need higher oversample — re-render
        frontend_->free_image(it->second.image_id);
        page_cache_.erase(it);
      }
    }

    frontend_->statusline("Rendering page " + std::to_string(i + 1) + "...", "");

    Pixmap pixmap = [&] {
      Stopwatch sw("mupdf render page " + std::to_string(i) + " zoom=" + std::to_string(render_zoom) + " os=" + std::to_string(os));
      return doc_.render_page(i, render_zoom);
    }();

    if (theme_ == Theme::DARK) {
      pixmap.invert();
    }

    int cols = (pixmap.width() + client.cell.width - 1) / client.cell.width;
    int rows = (pixmap.height() + client.cell.height - 1) / client.cell.height;

    uint32_t image_id = frontend_->upload_image(pixmap, cols, rows);
    page_cache_[i] = {image_id, {pixmap.width(), pixmap.height()}, {cols, rows}, os, render_zoom};
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

void App::update_statusline() {
  std::string left;
  if (input_mode_ == InputMode::COMMAND) {
    left = ":" + command_input_;
  }
  else if (input_mode_ == InputMode::SEARCH) {
    left = "/" + search_term_;
    if (search_total_matches_ > 0) {
      left += "  " + std::to_string(search_page_matches_) + "/" + std::to_string(search_total_matches_);
    }
  }
  else if (!last_action_.empty()) {
    auto elapsed = std::chrono::steady_clock::now() - last_action_time_;
    if (elapsed < std::chrono::seconds(1)) {
      left = last_action_;
    }
    else {
      last_action_.clear();
    }
  }

  auto mode_str = [&]() -> const char* {
    switch (view_mode_) {
      case ViewMode::CONTINUOUS:
        return "Continuous";
      case ViewMode::PAGE_WIDTH:
        return "Page-Width";
      case ViewMode::PAGE_HEIGHT:
        return "Page-Height";
      case ViewMode::SIDE_BY_SIDE:
        return "Side-by-Side";
    }
    return "";
  }();
  std::string theme_str = (theme_ == Theme::DARK) ? "DARK" : "LIGHT";
  int zoom_pct = static_cast<int>(user_zoom_ * 100.0f + 0.5f);
  int page = current_page() + 1;
  int total = doc_.page_count();
  std::string right = std::string(mode_str);
  if (zoom_pct != 100) {
    right += " \xe2\x94\x82 " + std::to_string(zoom_pct) + "%";
  }
  right += " \xe2\x94\x82 " + theme_str + " \xe2\x94\x82 " + std::to_string(page) + "/" + std::to_string(total);

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
    if (cached.oversample == 0) {
      img_scale = 1.0f; // NEVER mode: image IS the zoomed content
    }
    else {
      img_scale = static_cast<float>(cached.oversample) / user_zoom_;
    }

    // Where the actual rendered content starts in viewport coordinates.
    int content_top = page_top;
    int dst_col = 0;

    if (view_mode_ == ViewMode::PAGE_HEIGHT) {
      int vert_pad = std::max(0, (lay.rect.height - zoomed_h) / 2);
      content_top = page_top + vert_pad;
      if (zoomed_w < vw) {
        dst_col = (vw - zoomed_w) / 2 / client.cell.width;
      }
    }
    else if (view_mode_ == ViewMode::SIDE_BY_SIDE) {
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

static const char* command_label(Command cmd) {
  switch (cmd) {
    case Command::QUIT:
      return "Quit";
    case Command::RESIZE:
      return "Resize";
    case Command::SCROLL_DOWN:
      return "Scroll Down";
    case Command::SCROLL_UP:
      return "Scroll Up";
    case Command::HALF_PAGE_DOWN:
      return "Half Page Down";
    case Command::HALF_PAGE_UP:
      return "Half Page Up";
    case Command::GOTO_FIRST_PAGE:
      return "First Page";
    case Command::GOTO_LAST_PAGE:
      return "Last Page";
    case Command::PAGE_DOWN:
      return "Page Down";
    case Command::PAGE_UP:
      return "Page Up";
    case Command::SCROLL_LEFT:
      return "Scroll Left";
    case Command::SCROLL_RIGHT:
      return "Scroll Right";
    case Command::TOGGLE_VIEW_MODE:
      return "Toggle View";
    case Command::TOGGLE_THEME:
      return "Toggle Theme";
    case Command::ZOOM_IN:
      return "Zoom In";
    case Command::ZOOM_OUT:
      return "Zoom Out";
    case Command::ZOOM_RESET:
      return "Fit Width";
  }
  return "";
}

/// @brief A simple key-to-command binding dispatched from the main event loop.
struct KeyBinding {
  uint32_t key;      ///< Key ID (character code or control code).
  Command command;   ///< Command to dispatch.
  const char* label; ///< Display label for the help overlay.
};

/// @brief Display-only help entry for bindings with special dispatch logic.
struct HelpEntry {
  const char* key_label;   ///< Key combination display string.
  const char* description; ///< What the binding does.
};

/// Simple key-to-command bindings (order determines help display order).
static const KeyBinding KEY_BINDINGS[] = {
    {'j', Command::SCROLL_DOWN, "j"},
    {input::ARROW_DOWN, Command::SCROLL_DOWN, "\xe2\x86\x93"},
    {'k', Command::SCROLL_UP, "k"},
    {input::ARROW_UP, Command::SCROLL_UP, "\xe2\x86\x91"},
    {'d', Command::HALF_PAGE_DOWN, "d"},
    {'u', Command::HALF_PAGE_UP, "u"},
    {0x06, Command::PAGE_DOWN, "Ctrl+F"},
    {0x02, Command::PAGE_UP, "Ctrl+B"},
    {'h', Command::SCROLL_LEFT, "h"},
    {input::ARROW_LEFT, Command::SCROLL_LEFT, "\xe2\x86\x90"},
    {'l', Command::SCROLL_RIGHT, "l"},
    {input::ARROW_RIGHT, Command::SCROLL_RIGHT, "\xe2\x86\x92"},
    {'+', Command::ZOOM_IN, "+"},
    {'=', Command::ZOOM_IN, "="},
    {'-', Command::ZOOM_OUT, "-"},
    {'0', Command::ZOOM_RESET, "0"},
    {'w', Command::ZOOM_RESET, "w"},
    {0x09, Command::TOGGLE_VIEW_MODE, "Tab"},
    {'t', Command::TOGGLE_THEME, "t"},
    {'q', Command::QUIT, "q"},
};

/// Bindings with special dispatch logic (multi-key sequences, modes).
static const HelpEntry SPECIAL_HELP[] = {
    {"gg", "First Page"},
    {"G", "Last Page"},
    {"[n]gg / [n]G", "Go to Page n"},
    {":", "Command Mode"},
    {"o", "Table of Contents"},
    {"?", "Toggle Help"},
};

void App::run() {
  frontend_->clear();
  render();
  last_activity_time_ = std::chrono::steady_clock::now();

  while (running_) {
    auto event = frontend_->poll_input(100);
    if (!event) {
      auto client = frontend_->client_info();
      if (client.pixel != layout_size_) {
        spdlog::info("idle resize: pixel {} -> {}", layout_size_, client.pixel);
        frontend_->clear();
        render();
        last_activity_time_ = std::chrono::steady_clock::now();
      }
      else {
        // Defer pre-uploads until the user has been idle long enough.
        // Each upload blocks input for hundreds of ms, so avoid starting
        // one right after a render or keypress.
        auto idle = std::chrono::steady_clock::now() - last_activity_time_;
        if (idle >= std::chrono::milliseconds(300)) {
          pre_upload_adjacent();
        }
        if (!last_action_.empty() && std::chrono::steady_clock::now() - last_action_time_ >= std::chrono::seconds(1)) {
          last_action_.clear();
          update_statusline();
        }
      }
      continue;
    }

    last_activity_time_ = std::chrono::steady_clock::now();

    spdlog::debug("input: id={} (0x{:x}) modifiers={} type={}", event->id, event->id, event->modifiers, static_cast<int>(event->type));
    bool is_press = event->type == EventType::PRESS || event->type == EventType::UNKNOWN;

    if (!is_press) {
      continue;
    }

    // Dismiss help overlay on any key
    if (input_mode_ == InputMode::HELP) {
      input_mode_ = InputMode::NORMAL;
      frontend_->clear_overlay();
      update_viewport();
      update_statusline();
      continue;
    }

    // Outline mode input
    if (input_mode_ == InputMode::OUTLINE) {
      if (event->id == 27 || event->id == 'q') { // Escape or q
        input_mode_ = InputMode::NORMAL;
        frontend_->clear_overlay();
        update_viewport();
        update_statusline();
      }
      else if (event->id == 0x0E || event->id == input::ARROW_DOWN) { // Ctrl+N or Down
        outline_navigate(1);
      }
      else if (event->id == 0x10 || event->id == input::ARROW_UP) { // Ctrl+P or Up
        outline_navigate(-1);
      }
      else if (event->id == input::PAGE_DOWN) {
        int page_size = std::max(1, frontend_->client_info().rows * 3 / 4 - 4);
        outline_navigate(page_size);
      }
      else if (event->id == input::PAGE_UP) {
        int page_size = std::max(1, frontend_->client_info().rows * 3 / 4 - 4);
        outline_navigate(-page_size);
      }
      else if (event->id == input::HOME) {
        outline_navigate(-static_cast<int>(filtered_indices_.size()));
      }
      else if (event->id == input::END) {
        outline_navigate(static_cast<int>(filtered_indices_.size()));
      }
      else if (event->id == '\n' || event->id == '\r') {
        outline_jump();
      }
      else if (event->id == 127 || event->id == 8 || event->id == input::BACKSPACE) {
        if (!outline_filter_.empty()) {
          outline_filter_.pop_back();
          outline_apply_filter();
          show_outline();
        }
      }
      else if (event->id >= 32 && event->id < 127) {
        outline_filter_ += static_cast<char>(event->id);
        outline_apply_filter();
        show_outline();
      }
      continue;
    }

    // Enter command mode on ':'
    if (input_mode_ == InputMode::NORMAL && event->id == ':') {
      input_mode_ = InputMode::COMMAND;
      command_input_.clear();
      update_statusline();
      continue;
    }

    // Command mode text input
    if (input_mode_ == InputMode::COMMAND) {
      if (event->id == 27) { // Escape
        input_mode_ = InputMode::NORMAL;
        command_input_.clear();
        update_statusline();
      }
      else if (event->id == '\n' || event->id == '\r') {
        execute_command();
        input_mode_ = InputMode::NORMAL;
        command_input_.clear();
        update_statusline();
      }
      else if (event->id == 127 || event->id == 8 || event->id == input::BACKSPACE) {
        if (!command_input_.empty()) {
          command_input_.pop_back();
        }
        update_statusline();
      }
      else if (event->id >= 32 && event->id < 127) { // Printable ASCII
        command_input_ += static_cast<char>(event->id);
        update_statusline();
      }
      continue;
    }

    // Digits accumulate a count prefix (like vim)
    if (event->id >= '1' && event->id <= '9') {
      pending_count_ = pending_count_ * 10 + static_cast<int>(event->id - '0');
      pending_g_ = false;
      continue;
    }
    if (event->id == '0' && pending_count_ > 0) {
      pending_count_ = pending_count_ * 10;
      pending_g_ = false;
      continue;
    }

    // Handle gg / {count}gg
    if (event->id == 'g' && event->modifiers == 0) {
      if (pending_g_) {
        pending_g_ = false;
        if (pending_count_ > 0) {
          jump_to_page(pending_count_ - 1);
          pending_count_ = 0;
        }
        else {
          handle_command(Command::GOTO_FIRST_PAGE);
        }
      }
      else {
        pending_g_ = true;
      }
      continue;
    }

    // Handle G / {count}G
    if (event->id == 'G' && event->modifiers == 0) {
      if (pending_count_ > 0) {
        jump_to_page(pending_count_ - 1);
        pending_count_ = 0;
      }
      else {
        handle_command(Command::GOTO_LAST_PAGE);
      }
      pending_g_ = false;
      continue;
    }

    // Any other key resets pending state
    pending_g_ = false;
    pending_count_ = 0;

    // Dispatch simple key bindings from the table
    bool handled = false;
    for (const auto& kb : KEY_BINDINGS) {
      if (event->id == kb.key) {
        handle_command(kb.command);
        handled = true;
        break;
      }
    }

    if (!handled) {
      if (event->id == '?') {
        input_mode_ = InputMode::HELP;
        show_help();
      }
      else if (event->id == 'o') {
        if (outline_.empty()) {
          outline_ = doc_.load_outline();
        }
        if (outline_.empty()) {
          last_action_ = "No outline";
          last_action_time_ = std::chrono::steady_clock::now();
        }
        else {
          input_mode_ = InputMode::OUTLINE;
          outline_cursor_ = 0;
          outline_scroll_ = 0;
          outline_filter_.clear();
          outline_apply_filter();
          show_outline();
        }
      }
      else if (event->id == input::RESIZE) {
        handle_command(Command::RESIZE);
      }
    }
  }
}

void App::handle_command(Command cmd) {
  last_action_ = command_label(cmd);
  last_action_time_ = std::chrono::steady_clock::now();

  switch (cmd) {
    case Command::QUIT:
      spdlog::info("quit");
      running_ = false;
      break;
    case Command::RESIZE: {
      auto client = frontend_->client_info();
      spdlog::info("resize: {} px, {} cell", client.pixel, client.cell);
      frontend_->clear();
      render();
      break;
    }
    case Command::SCROLL_DOWN: {
      auto client = frontend_->client_info();
      scroll(0, client.cell.height);
      break;
    }
    case Command::SCROLL_UP: {
      auto client = frontend_->client_info();
      scroll(0, -client.cell.height);
      break;
    }
    case Command::HALF_PAGE_DOWN: {
      auto client = frontend_->client_info();
      scroll(0, client.viewport_pixel().height / 2);
      break;
    }
    case Command::HALF_PAGE_UP: {
      auto client = frontend_->client_info();
      scroll(0, -client.viewport_pixel().height / 2);
      break;
    }
    case Command::GOTO_FIRST_PAGE: {
      spdlog::info("goto first page");
      jump_to_page(0);
      break;
    }
    case Command::GOTO_LAST_PAGE: {
      spdlog::info("goto last page (bottom)");
      if (!layout_.empty()) {
        auto client = frontend_->client_info();
        int vh = client.viewport_pixel().height;
        int total_height = document_height();
        scroll_.y = std::max(0, total_height - vh);
        update_viewport();
      }
      break;
    }
    case Command::PAGE_DOWN: {
      if (layout_.empty()) {
        break;
      }
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
      break;
    }
    case Command::PAGE_UP: {
      if (layout_.empty()) {
        break;
      }
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
      break;
    }
    case Command::SCROLL_LEFT: {
      auto client = frontend_->client_info();
      scroll(-client.cell.width, 0);
      break;
    }
    case Command::SCROLL_RIGHT: {
      auto client = frontend_->client_info();
      scroll(client.cell.width, 0);
      break;
    }
    case Command::TOGGLE_VIEW_MODE: {
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
      break;
    }
    case Command::TOGGLE_THEME: {
      theme_ = (theme_ == Theme::DARK) ? Theme::LIGHT : Theme::DARK;
      frontend_->clear();
      render();
      break;
    }
    case Command::ZOOM_IN: {
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
      break;
    }
    case Command::ZOOM_OUT: {
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
      break;
    }
    case Command::ZOOM_RESET: {
      float old = user_zoom_;
      user_zoom_ = 1.0f;
      scroll_.x = 0;
      spdlog::info("zoom reset: 100%");
      handle_zoom_change(old);
      break;
    }
  }
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
      last_action_ = "Invalid page number";
      last_action_time_ = std::chrono::steady_clock::now();
    }
  }
  else if (name == "q" || name == "quit") {
    running_ = false;
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
      if (value == "dark") {
        theme_ = Theme::DARK;
        frontend_->clear();
        render();
      }
      else if (value == "light") {
        theme_ = Theme::LIGHT;
        frontend_->clear();
        render();
      }
      else {
        last_action_ = "Unknown theme: " + value;
        last_action_time_ = std::chrono::steady_clock::now();
      }
    }
    else if (key == "mode") {
      ViewMode new_mode = view_mode_;
      if (value == "continuous") {
        new_mode = ViewMode::CONTINUOUS;
      }
      else if (value == "page-width") {
        new_mode = ViewMode::PAGE_WIDTH;
      }
      else if (value == "page-height") {
        new_mode = ViewMode::PAGE_HEIGHT;
      }
      else if (value == "side-by-side") {
        new_mode = ViewMode::SIDE_BY_SIDE;
      }
      else {
        last_action_ = "Unknown mode: " + value;
        last_action_time_ = std::chrono::steady_clock::now();
        return;
      }
      view_mode_ = new_mode;
      user_zoom_ = 1.0f;
      scroll_.x = 0;
      render();
    }
    else if (key == "oversample") {
      if (value == "auto") {
        oversample_setting_ = Oversample::AUTO;
      }
      else if (value == "never") {
        oversample_setting_ = Oversample::NEVER;
      }
      else if (value == "1") {
        oversample_setting_ = Oversample::X1;
      }
      else if (value == "2") {
        oversample_setting_ = Oversample::X2;
      }
      else if (value == "4") {
        oversample_setting_ = Oversample::X4;
      }
      else {
        last_action_ = "Unknown oversample: " + value;
        last_action_time_ = std::chrono::steady_clock::now();
        return;
      }
      frontend_->clear();
      render();
    }
    else {
      last_action_ = "Unknown setting: " + key;
      last_action_time_ = std::chrono::steady_clock::now();
    }
  }
  else {
    last_action_ = "Unknown: " + name;
    last_action_time_ = std::chrono::steady_clock::now();
  }
}

void App::show_help() {
  // Find the widest key label for alignment
  int max_key_len = 0;
  for (const auto& kb : KEY_BINDINGS) {
    max_key_len = std::max(max_key_len, static_cast<int>(std::strlen(kb.label)));
  }
  for (const auto& he : SPECIAL_HELP) {
    max_key_len = std::max(max_key_len, static_cast<int>(std::strlen(he.key_label)));
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

  for (const auto& kb : KEY_BINDINGS) {
    lines.push_back(format_line(kb.label, command_label(kb.command)));
  }
  for (const auto& he : SPECIAL_HELP) {
    lines.push_back(format_line(he.key_label, he.description));
  }

  frontend_->show_overlay(lines);
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
  show_outline();
}

void App::outline_jump() {
  if (filtered_indices_.empty()) {
    return;
  }
  int page = outline_[filtered_indices_[outline_cursor_]].page;
  input_mode_ = InputMode::NORMAL;
  frontend_->clear_overlay();
  jump_to_page(page);
  update_statusline();
}

void App::show_outline() {
  auto client = frontend_->client_info();

  // Fixed 75% of terminal dimensions (minus status bar row and gap row)
  int box_lines = std::max(4, client.rows * 3 / 4 - 2);
  int box_width = std::max(20, client.cols * 3 / 4);

  // Header takes 2 lines (title + filter/blank)
  int max_visible = box_lines - 2;

  // Scroll window follows cursor
  if (outline_cursor_ < outline_scroll_) {
    outline_scroll_ = outline_cursor_;
  }
  if (outline_cursor_ >= outline_scroll_ + max_visible) {
    outline_scroll_ = outline_cursor_ - max_visible + 1;
  }

  // Pad a line to box_width (accounting for ANSI escapes not taking display space).
  // The overlay adds 4 chars padding (2 each side), so content width = box_width - 4.
  int content_w = box_width - 4;
  auto pad_line = [content_w](const std::string& text, int visible_len) -> std::string {
    if (visible_len >= content_w) {
      return text;
    }
    return text + std::string(content_w - visible_len, ' ');
  };

  std::vector<std::string> lines;
  lines.push_back(pad_line("Table of Contents", 17));

  if (!outline_filter_.empty()) {
    std::string filter_line = "> " + outline_filter_;
    lines.push_back(pad_line(filter_line, static_cast<int>(filter_line.size())));
  }
  else {
    lines.push_back(pad_line("", 0));
  }

  int end = std::min(outline_scroll_ + max_visible, static_cast<int>(filtered_indices_.size()));
  for (int vi = outline_scroll_; vi < end; ++vi) {
    const auto& entry = outline_[filtered_indices_[vi]];
    std::string indent(entry.level * 2, ' ');
    std::string page_str = std::to_string(entry.page + 1);
    std::string title_part = indent + entry.title;
    int title_len = static_cast<int>(title_part.size());
    int page_len = static_cast<int>(page_str.size());
    int gap = std::max(1, content_w - title_len - page_len);
    std::string visible_text = title_part + std::string(gap, ' ') + page_str;

    if (vi == outline_cursor_) {
      std::string line = "\x1b[0m\x1b[1;4m" + visible_text + "\x1b[0m\x1b[7m";
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

  frontend_->show_overlay(lines);
}
