#include "app.h"

#include "input_event.h"
#include "stopwatch.h"

#include <spdlog/spdlog.h>

#include <algorithm>

App::App(std::unique_ptr<Frontend> frontend, const Args& args)
    : frontend_(std::move(frontend))
    , doc_(args.file) {
  spdlog::info("opened document: {} pages", doc_.page_count());
}

void App::build_layout() {
  auto [pxy, pxx] = frontend_->pixel_size();
  int num_pages = doc_.page_count();
  layout_.resize(num_pages);

  int y = 0;
  for (int i = 0; i < num_pages; ++i) {
    auto [page_w, page_h] = doc_.page_size(i);
    float zoom = static_cast<float>(pxx) / page_w;
    int h = static_cast<int>(page_h * zoom);
    layout_[i] = {y, h, zoom};
    y += h + PAGE_GAP_PX;
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
    if (layout_[mid].global_y <= y) {
      lo = mid;
    }
    else {
      hi = mid - 1;
    }
  }
  return lo;
}

void App::ensure_pages_uploaded(int first, int last) {
  auto [celly, cellx] = frontend_->cell_size();
  if (celly == 0 || cellx == 0) {
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

    int cols = (pixmap.width() + static_cast<int>(cellx) - 1) / static_cast<int>(cellx);
    int rows = (pixmap.height() + static_cast<int>(celly) - 1) / static_cast<int>(celly);

    uint32_t image_id = frontend_->upload_image(pixmap, cols, rows);
    page_cache_[i] = {image_id, pixmap.width(), pixmap.height(), cols, rows};
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

void App::update_viewport() {
  auto [pxy, pxx] = frontend_->pixel_size();
  auto [celly, cellx] = frontend_->cell_size();
  if (celly == 0 || cellx == 0 || layout_.empty()) {
    return;
  }

  int vh = static_cast<int>(pxy);
  int vw = static_cast<int>(pxx);
  int num_pages = static_cast<int>(layout_.size());

  // Find visible pages
  int first = page_at_y(scroll_y_);
  int last = first;
  for (int i = first; i < num_pages; ++i) {
    if (layout_[i].global_y >= scroll_y_ + vh) {
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

    // Page position relative to viewport
    int page_top = lay.global_y - scroll_y_;
    int page_bottom = page_top + lay.pixel_height;

    // Clamp to viewport
    int vis_top = std::max(page_top, 0);
    int vis_bottom = std::min(page_bottom, vh);
    if (vis_top >= vis_bottom) {
      continue;
    }

    // Source rect in page image
    int src_y = vis_top - page_top;
    int src_h = vis_bottom - vis_top;

    // Destination in cell rows
    int dst_row = vis_top / static_cast<int>(celly);
    int dst_rows = (vis_bottom + static_cast<int>(celly) - 1) / static_cast<int>(celly) - dst_row;

    slices.push_back({cached.image_id, 0, src_y, vw, src_h, dst_row, dst_rows, cached.cell_cols, cached.cell_rows});
  }

  frontend_->show_pages(slices);
}

void App::scroll(int dx, int dy) {
  if (layout_.empty()) {
    return;
  }

  auto [pxy, pxx] = frontend_->pixel_size();
  int vh = static_cast<int>(pxy);

  // Total document height
  const auto& last_layout = layout_.back();
  int total_height = last_layout.global_y + last_layout.pixel_height;
  int max_y = std::max(0, total_height - vh);

  int new_y = std::clamp(scroll_y_ + dy, 0, max_y);
  int new_x = scroll_x_ + dx; // horizontal scroll not clamped yet

  if (new_x == scroll_x_ && new_y == scroll_y_) {
    return;
  }

  scroll_x_ = new_x;
  scroll_y_ = new_y;
  update_viewport();
}

void App::render() {
  // Clear all cached pages
  for (auto& [page, cached] : page_cache_) {
    frontend_->free_image(cached.image_id);
  }
  page_cache_.clear();

  build_layout();

  // Clamp scroll
  if (!layout_.empty()) {
    auto [pxy, pxx] = frontend_->pixel_size();
    int vh = static_cast<int>(pxy);
    const auto& last_layout = layout_.back();
    int total_height = last_layout.global_y + last_layout.pixel_height;
    int max_y = std::max(0, total_height - vh);
    scroll_y_ = std::clamp(scroll_y_, 0, max_y);
  }

  update_viewport();
}

void App::jump_to_page(int page) {
  int num_pages = static_cast<int>(layout_.size());
  page = std::clamp(page, 0, num_pages - 1);
  scroll_y_ = layout_[page].global_y;

  // Clamp
  auto [pxy, pxx] = frontend_->pixel_size();
  int vh = static_cast<int>(pxy);
  const auto& last_layout = layout_.back();
  int total_height = last_layout.global_y + last_layout.pixel_height;
  int max_y = std::max(0, total_height - vh);
  scroll_y_ = std::clamp(scroll_y_, 0, max_y);

  update_viewport();
}

void App::run() {
  frontend_->clear();
  render();

  while (running_) {
    auto event = frontend_->poll_input(100);
    if (!event) {
      pre_upload_adjacent();
      continue;
    }

    spdlog::debug("input: id={} (0x{:x}) modifiers={} type={}", event->id, event->id, event->modifiers, static_cast<int>(event->type));
    bool is_press = event->type == EventType::PRESS || event->type == EventType::UNKNOWN;

    if (!is_press) {
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

    if (event->id == 'q' && event->modifiers == 0) {
      handle_command(Command::QUIT);
    }
    else if (event->id == 'j') {
      handle_command(Command::SCROLL_DOWN);
    }
    else if (event->id == 'k') {
      handle_command(Command::SCROLL_UP);
    }
    else if (event->id == 'd') {
      handle_command(Command::HALF_PAGE_DOWN);
    }
    else if (event->id == 'u') {
      handle_command(Command::HALF_PAGE_UP);
    }
    else if (event->id == input::RESIZE) {
      handle_command(Command::RESIZE);
    }
  }
}

void App::handle_command(Command cmd) {
  switch (cmd) {
    case Command::QUIT:
      spdlog::info("quit");
      running_ = false;
      break;
    case Command::RESIZE: {
      auto [pxy, pxx] = frontend_->pixel_size();
      auto [celly, cellx] = frontend_->cell_size();
      spdlog::info("resize: {}x{} px, {}x{} cell", pxx, pxy, cellx, celly);
      frontend_->clear();
      render();
      break;
    }
    case Command::SCROLL_DOWN: {
      auto [celly, cellx] = frontend_->cell_size();
      scroll(0, static_cast<int>(celly));
      break;
    }
    case Command::SCROLL_UP: {
      auto [celly, cellx] = frontend_->cell_size();
      scroll(0, -static_cast<int>(celly));
      break;
    }
    case Command::HALF_PAGE_DOWN: {
      auto [pxy, pxx] = frontend_->pixel_size();
      scroll(0, static_cast<int>(pxy) / 2);
      break;
    }
    case Command::HALF_PAGE_UP: {
      auto [pxy, pxx] = frontend_->pixel_size();
      scroll(0, -static_cast<int>(pxy) / 2);
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
        auto [pxy, pxx] = frontend_->pixel_size();
        int vh = static_cast<int>(pxy);
        const auto& last = layout_.back();
        int total_height = last.global_y + last.pixel_height;
        scroll_y_ = std::max(0, total_height - vh);
        update_viewport();
      }
      break;
    }
  }
}
