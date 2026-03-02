#include "app.h"

#include "input_event.h"
#include "stopwatch.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <string>

App::App(std::unique_ptr<Frontend> frontend, const Args& args)
    : frontend_(std::move(frontend))
    , doc_(args.file) {
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
  spdlog::debug("build_layout: pixel={} vp={} cell={} pages={}", client.pixel, vp, client.cell, num_pages);
  layout_.resize(num_pages);

  if (view_mode_ == ViewMode::PAGE_HEIGHT) {
    for (int i = 0; i < num_pages; ++i) {
      auto [page_w, page_h] = doc_.page_size(i);
      float zoom = std::min(static_cast<float>(vw) / page_w, static_cast<float>(vh) / page_h);
      int rendered_w = static_cast<int>(page_w * zoom);
      layout_[i] = {{0, i * vh, rendered_w, vh}, zoom};
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
      if (i % 2 == 0) {
        layout_[i] = {{0, y, rendered_w, vh}, zoom};
      }
      else {
        layout_[i] = {{0, layout_[i - 1].rect.y, rendered_w, vh}, zoom};
        y += vh;
      }
    }
    if (num_pages > 0 && num_pages % 2 != 0) {
      y += vh;
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
      int h = static_cast<int>(page_h * zoom);
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

void App::ensure_pages_uploaded(int first, int last) {
  auto client = frontend_->client_info();
  if (!client.is_valid()) {
    return;
  }

  for (int i = first; i <= last; ++i) {
    if (page_cache_.count(i) > 0) {
      continue;
    }

    float zoom = layout_[i].zoom;

    Pixmap pixmap = [&] {
      Stopwatch sw("mupdf render page " + std::to_string(i));
      return doc_.render_page(i, zoom);
    }();

    int cols = (pixmap.width() + client.cell.width - 1) / client.cell.width;
    int rows = (pixmap.height() + client.cell.height - 1) / client.cell.height;

    uint32_t image_id = frontend_->upload_image(pixmap, cols, rows);
    page_cache_[i] = {image_id, {pixmap.width(), pixmap.height()}, {cols, rows}};
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
  int page = current_page() + 1;
  int total = doc_.page_count();
  std::string right = std::string(mode_str) + " \xe2\x94\x82 " + theme_str + " \xe2\x94\x82 " + std::to_string(page) + "/" + std::to_string(total);

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

  ensure_pages_uploaded(first, last);
  evict_distant_pages(first, last);

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

    // Rendered page dimensions at this zoom
    auto [page_w, page_h] = doc_.page_size(i);
    int rendered_w = static_cast<int>(page_w * lay.zoom);
    int rendered_h = static_cast<int>(page_h * lay.zoom);

    // Where the actual rendered content starts in viewport coordinates.
    // For centered modes this differs from page_top due to padding.
    int content_top = page_top;
    int dst_col = 0;

    if (view_mode_ == ViewMode::PAGE_HEIGHT) {
      int vert_pad = (vh - rendered_h) / 2;
      content_top = page_top + vert_pad;
      dst_col = (vw - rendered_w) / 2 / client.cell.width;
    }
    else if (view_mode_ == ViewMode::SIDE_BY_SIDE) {
      int vert_pad = (vh - rendered_h) / 2;
      content_top = page_top + vert_pad;
      bool is_last_odd = (i == num_pages - 1) && (i % 2 == 0);
      if (is_last_odd) {
        dst_col = (vw - rendered_w) / 2 / client.cell.width;
      }
      else if (i % 2 == 0) {
        int half_w = vw / 2;
        dst_col = (half_w - rendered_w) / 2 / client.cell.width;
      }
      else {
        int half_w = vw / 2;
        dst_col = (half_w + (half_w - rendered_w) / 2) / client.cell.width;
      }
    }

    // Clamp to viewport
    PixelRect content_rect = {0, content_top, rendered_w, rendered_h};
    PixelRect viewport_rect = {0, 0, vw, vh};
    auto visible = content_rect.intersect(viewport_rect);
    if (visible.height <= 0) {
      continue;
    }

    // Source rect — offset into the rendered image
    int src_x = std::clamp(scroll_.x, 0, std::max(0, rendered_w - vw));
    int src_y = visible.top() - content_top;
    PixelRect src = {src_x, src_y, rendered_w, visible.height};

    // Destination in cells
    int dst_row = visible.top() / client.cell.height;
    int dst_rows = (visible.bottom() + client.cell.height - 1) / client.cell.height - dst_row;
    CellRect dst = {dst_col, dst_row, 0, dst_rows};

    slices.push_back({cached.image_id, src, dst, cached.cell_grid});
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

  int new_x = std::max(0, scroll_.x + dx);

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
    int total_height = document_height();
    int max_y = std::max(0, total_height - vh);
    scroll_.y = std::clamp(scroll_.y, 0, max_y);
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
    {'k', Command::SCROLL_UP, "k"},
    {'d', Command::HALF_PAGE_DOWN, "d"},
    {'u', Command::HALF_PAGE_UP, "u"},
    {0x06, Command::PAGE_DOWN, "Ctrl+F"},
    {0x02, Command::PAGE_UP, "Ctrl+B"},
    {'h', Command::SCROLL_LEFT, "h"},
    {'l', Command::SCROLL_RIGHT, "l"},
    {0x09, Command::TOGGLE_VIEW_MODE, "Tab"},
    {'t', Command::TOGGLE_THEME, "t"},
    {'q', Command::QUIT, "q"},
};

/// Bindings with special dispatch logic (multi-key sequences, modes).
static const HelpEntry SPECIAL_HELP[] = {
    {"gg", "First Page"},
    {"G", "Last Page"},
    {"[n]gg / [n]G", "Go to Page n"},
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
