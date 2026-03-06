#include "app.hpp"

#include "action.hpp"
#include "args.hpp"
#include "color.hpp"
#include "converter.hpp"
#include "document.hpp"
#include "frontend.hpp"
#include "geometry.hpp"
#include "page.hpp"

#include <spdlog/spdlog.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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
      return "light";
    case Theme::Dark:
      return "dark";
    case Theme::Auto:
      return (effective == Theme::Dark) ? "auto/dark" : "auto/light";
    case Theme::Terminal:
      return "terminal";
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
    , render_scale_setting_(args.render_scale)
    , page_manager_(args.max_page_cache)
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
    int y = 0;
    for (int i = 0; i < num_pages; ++i) {
      auto ps = doc_.page_size(i);
      float zoom = static_cast<float>(vw) / ps.width;
      int h = static_cast<int>(ps.height * zoom * user_zoom_);
      layout_[i] = {{0, y, vw, h}, zoom};
      y += h;
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

RenderParams App::make_render_params() const {
  auto [fg, bg] = resolve_recolor_colors();
  auto client = frontend_->client_info();
  auto theme = effective_theme();
  Color sep = colors_.page_separator;
  if (sep.is_default) {
    switch (theme) {
      case Theme::Light:
        sep = Color::rgb(160, 160, 160);
        break;
      case Theme::Dark:
        sep = Color::rgb(100, 100, 100);
        break;
      case Theme::Terminal: {
        auto mix = [](uint8_t a, uint8_t b) -> uint8_t { return static_cast<uint8_t>(a + (b - a) * 50 / 100); };
        sep = Color::rgb(mix(bg.r, fg.r), mix(bg.g, fg.g), mix(bg.b, fg.b));
        break;
      }
      default:
        sep = Color::rgb(100, 100, 100);
        break;
    }
  }
  return {
      user_zoom_,
      effective_render_scale(),
      theme,
      fg,
      bg,
      colors_.recolor_accent,
      client.cell,
      sep,
      view_mode_ == ViewMode::Continuous,
  };
}

HighlightParams App::make_highlight_params() const {
  return {
      &search_.results,
      &colors_,
      app_mode_,
      selection_anchor_,
      selection_extent_,
  };
}

int App::document_height() const {
  if (layout_.empty()) {
    return 0;
  }
  const auto& last = layout_.back();
  return last.rect.y + last.rect.height;
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

  page_manager_.evict_distant(first, last, static_cast<int>(layout_.size()), *frontend_);
  page_manager_.ensure_uploaded(first, last, doc_, *frontend_, layout_, make_render_params(), make_highlight_params());

  // Build PageSlice vector
  std::vector<PageSlice> slices;
  for (int i = first; i <= last; ++i) {
    const auto* entry = page_manager_.get(i);
    if (entry == nullptr) {
      continue;
    }
    const auto& cached = *entry;
    const auto& lay = layout_[i];

    int page_top = lay.rect.y - scroll_.y;

    // Zoomed page dimensions (what the user sees on screen)
    auto ps = doc_.page_size(i);
    int zoomed_w = static_cast<int>(ps.width * lay.zoom * user_zoom_);
    int zoomed_h = static_cast<int>(ps.height * lay.zoom * user_zoom_);

    // Scale factor: zoomed-content pixels -> image pixels
    float img_scale;
    if (cached.render_scale() == 0.0f) {
      img_scale = 1.0f; // NEVER mode: image IS the zoomed content
    }
    else {
      img_scale = cached.render_scale() / user_zoom_;
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
    PixelRect image_bounds = {0, 0, cached.pixel_size().width, cached.pixel_size().height};
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

    slices.push_back({cached.image_id(), src, dst, cached.cell_grid(), cached.pixel_size()});
  }

  spdlog::debug("update_viewport: pages [{},{}] slices={}", first, last, slices.size());

  // Wrap show_pages + statusline in CSI 2026 synchronized update to prevent
  // tmux from rendering intermediate frames (page content without statusline).
  static const char BSU[] = "\033[?2026h";
  frontend_->write_raw(BSU, sizeof(BSU) - 1);

  frontend_->show_pages(slices);

  // Fire observer after show_pages() so the statusline is the last thing rendered,
  // reflecting the final viewport state.
  if (state_observer_) {
    state_observer_();
  }

  static const char ESU[] = "\033[?2026l";
  frontend_->write_raw(ESU, sizeof(ESU) - 1);
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
  page_manager_.clear(*frontend_);

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
  if (layout_.empty()) {
    return;
  }
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
  render();
  last_activity_time_ = std::chrono::steady_clock::now();
}

void App::idle_tick() {
  // Debounced resize: render once quiet period elapses after last resize event.
  if (resize_pending_since_ != std::chrono::steady_clock::time_point{}) {
    auto elapsed = std::chrono::steady_clock::now() - resize_pending_since_;
    if (elapsed >= ResizeDebounce) {
      resize_pending_since_ = {};
      auto client = frontend_->client_info();
      spdlog::info("resize (debounced): {} px, {} cell", client.pixel, client.cell);

      // Rebuild layout without clearing the page cache. ensure_uploaded() will
      // re-render only pages whose zoom actually changed (e.g. width resize in
      // Continuous mode) and keep cached pages whose zoom still matches (e.g.
      // height-only resize).
      build_layout();

      if (!layout_.empty()) {
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
    return;
  }

  auto client = frontend_->client_info();
  if (client.pixel != layout_size_) {
    spdlog::info("idle resize: pixel {} -> {}", layout_size_, client.pixel);
    resize_pending_since_ = std::chrono::steady_clock::now();
  }
  else {
    auto idle = std::chrono::steady_clock::now() - last_activity_time_;
    if (idle >= std::chrono::milliseconds(300)) {
      page_manager_.pre_upload_one(
          viewport_first_page_,
          viewport_last_page_,
          static_cast<int>(layout_.size()),
          doc_,
          *frontend_,
          layout_,
          make_render_params(),
          make_highlight_params(),
          search_.results,
          search_.current
      );
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

void App::handle_action(const Action& act) {
  last_activity_time_ = std::chrono::steady_clock::now();

  std::visit(
      [](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, action::ScrollDown> || std::is_same_v<T, action::ScrollUp> || std::is_same_v<T, action::ScrollLeft>
                      || std::is_same_v<T, action::ScrollRight> || std::is_same_v<T, action::MouseScroll> || std::is_same_v<T, action::Resize>
                      || std::is_same_v<T, action::DragUpdate> || std::is_same_v<T, action::SelectionMove>) {
          spdlog::trace("action: {}", T::Name);
        }
        else {
          spdlog::debug("action: {}", T::Name);
        }
      },
      act
  );

  // Dismiss link hints on any action. LinkHintKey matches first, all others just exit.
  if (app_mode_ == AppMode::LinkHints) {
    if (auto* key = std::get_if<action::LinkHintKey>(&act)) {
      link_hint_input_ += key->ch;
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
      return;
    }
    exit_link_hints();
  }

  std::visit(
      [&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, action::Quit>) {
          spdlog::info("quit");
          running_ = false;
        }
        else if constexpr (std::is_same_v<T, action::Resize>) {
          resize_pending_since_ = std::chrono::steady_clock::now();
        }
        else if constexpr (std::is_same_v<T, action::ScrollDown>) {
          auto client = frontend_->client_info();
          int count = std::max(1, arg.count);
          scroll(0, client.cell.height * scroll_lines_ * count);
        }
        else if constexpr (std::is_same_v<T, action::ScrollUp>) {
          auto client = frontend_->client_info();
          int count = std::max(1, arg.count);
          scroll(0, -client.cell.height * scroll_lines_ * count);
        }
        else if constexpr (std::is_same_v<T, action::HalfPageDown>) {
          auto client = frontend_->client_info();
          scroll(0, client.viewport_pixel().height / 2);
        }
        else if constexpr (std::is_same_v<T, action::HalfPageUp>) {
          auto client = frontend_->client_info();
          scroll(0, -client.viewport_pixel().height / 2);
        }
        else if constexpr (std::is_same_v<T, action::PageDown>) {
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
        else if constexpr (std::is_same_v<T, action::PageUp>) {
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
        else if constexpr (std::is_same_v<T, action::ScrollLeft>) {
          auto client = frontend_->client_info();
          int count = std::max(1, arg.count);
          scroll(-client.cell.width * scroll_lines_ * count, 0);
        }
        else if constexpr (std::is_same_v<T, action::ScrollRight>) {
          auto client = frontend_->client_info();
          int count = std::max(1, arg.count);
          scroll(client.cell.width * scroll_lines_ * count, 0);
        }
        else if constexpr (std::is_same_v<T, action::GotoPage>) {
          jump_to_page(arg.page - 1);
        }
        else if constexpr (std::is_same_v<T, action::GotoFirstPage>) {
          spdlog::info("goto first page");
          jump_to_page(0);
        }
        else if constexpr (std::is_same_v<T, action::GotoLastPage>) {
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
        else if constexpr (std::is_same_v<T, action::ZoomIn>) {
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
        else if constexpr (std::is_same_v<T, action::ZoomOut>) {
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
        else if constexpr (std::is_same_v<T, action::ZoomReset>) {
          float old = user_zoom_;
          user_zoom_ = 1.0f;
          scroll_.x = 0;
          spdlog::info("zoom reset: 100%");
          handle_zoom_change(old);
        }
        else if constexpr (std::is_same_v<T, action::ToggleViewMode>) {
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
        else if constexpr (std::is_same_v<T, action::SetViewMode>) {
          apply_view_mode(arg.mode);
        }
        else if constexpr (std::is_same_v<T, action::ToggleTheme>) {
          if (theme_ == Theme::Dark) {
            theme_ = Theme::Light;
          }
          else if (theme_ == Theme::Light) {
            theme_ = Theme::Terminal;
          }
          else {
            theme_ = Theme::Dark;
          }
          render();
        }
        else if constexpr (std::is_same_v<T, action::SetTheme>) {
          apply_theme(arg.theme);
        }
        else if constexpr (std::is_same_v<T, action::SetRenderScale>) {
          apply_render_scale(arg.strategy);
        }
        else if constexpr (std::is_same_v<T, action::SetSidebarWidth>) {
        }
        else if constexpr (std::is_same_v<T, action::Reload>) {
          do_reload();
        }
        else if constexpr (std::is_same_v<T, action::JumpBack>) {
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
        else if constexpr (std::is_same_v<T, action::JumpForward>) {
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
        else if constexpr (std::is_same_v<T, action::Search>) {
          search_.term = arg.term;
          execute_search();
        }
        else if constexpr (std::is_same_v<T, action::SearchNext>) {
          if (!search_.empty()) {
            search_navigate(1);
          }
        }
        else if constexpr (std::is_same_v<T, action::SearchPrev>) {
          if (!search_.empty()) {
            search_navigate(-1);
          }
        }
        else if constexpr (std::is_same_v<T, action::ClearSearch>) {
          clear_search();
        }
        else if constexpr (std::is_same_v<T, action::EnterLinkHints>) {
          enter_link_hints();
        }
        else if constexpr (std::is_same_v<T, action::LinkHintKey> || std::is_same_v<T, action::LinkHintCancel>) {
          // Handled by the early link-hints dismissal block above.
        }
        else if constexpr (std::is_same_v<T, action::GetOutline>) {
          if (outline_.empty()) {
            outline_ = doc_.load_outline();
          }
        }
        else if constexpr (std::is_same_v<T, action::GetLinks>) {
          // Data is fetched by the caller via visible_links()
        }
        else if constexpr (std::is_same_v<T, action::GetState>) {
          // Data is fetched by the caller via view_state()
        }
        else if constexpr (std::is_same_v<T, action::Hide>) {
          frontend_->show_pages({});
        }
        else if constexpr (std::is_same_v<T, action::Show>) {
          update_viewport();
        }
        else if constexpr (std::is_same_v<T, action::MouseScroll>) {
          scroll(arg.dx, arg.dy);
        }
        else if constexpr (std::is_same_v<T, action::ClickAt>) {
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
        else if constexpr (std::is_same_v<T, action::EnterVisualMode>) {
          enter_visual_mode(false);
        }
        else if constexpr (std::is_same_v<T, action::EnterVisualBlockMode>) {
          enter_visual_mode(true);
        }
        else if constexpr (std::is_same_v<T, action::SelectionMove>) {
          move_selection_extent(arg.dx, arg.dy);
        }
        else if constexpr (std::is_same_v<T, action::SelectionMoveWord>) {
          move_selection_word(arg.direction);
        }
        else if constexpr (std::is_same_v<T, action::SelectionGoto>) {
          selection_goto(arg.target);
        }
        else if constexpr (std::is_same_v<T, action::SelectionYank>) {
          yank_selection();
        }
        else if constexpr (std::is_same_v<T, action::SelectionCancel>) {
          cancel_selection();
        }
        else if constexpr (std::is_same_v<T, action::DragStart>) {
          last_click_point_ = screen_to_page_point(arg.col, arg.row);
          has_click_point_ = (last_click_point_.page >= 0);
        }
        else if constexpr (std::is_same_v<T, action::DragUpdate>) {
          if (app_mode_ == AppMode::Normal && has_click_point_) {
            selection_anchor_ = last_click_point_;
            selection_extent_ = last_click_point_;
            app_mode_ = AppMode::Visual;
          }
          if (app_mode_ == AppMode::Visual || app_mode_ == AppMode::VisualBlock) {
            update_selection_extent(arg.col, arg.row);
          }
        }
        else if constexpr (std::is_same_v<T, action::DragEnd>) {
          if (app_mode_ == AppMode::Visual || app_mode_ == AppMode::VisualBlock) {
            update_selection_extent(arg.col, arg.row);
          }
        }
        // Terminal-only commands handled by TerminalController, not App.
        else if constexpr (std::is_same_v<T, action::OpenOutline>) {
        }
        else if constexpr (std::is_same_v<T, action::ToggleSidebar>) {
        }
        else if constexpr (std::is_same_v<T, action::EnterCommandMode>) {
        }
        else if constexpr (std::is_same_v<T, action::EnterSearchMode>) {
        }
        else if constexpr (std::is_same_v<T, action::ShowHelp>) {
        }
      },
      act
  );

  if (state_observer_) {
    state_observer_();
  }
}

void App::set_state_observer(std::function<void()> observer) {
  state_observer_ = std::move(observer);
}

void App::do_reload() {
  try {
    page_manager_.clear(*frontend_);

    doc_.reload(file_path_);

    outline_.clear();
    search_.clear();

    build_layout();

    auto client = frontend_->client_info();
    int vh = client.viewport_pixel().height;
    int total_height = document_height();
    int max_y = std::max(0, total_height - vh);
    scroll_.y = std::clamp(scroll_.y, 0, max_y);

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
  render();
}

void App::execute_search() {
  if (search_.term.empty()) {
    return;
  }

  search_.results = doc_.search(search_.term);
  search_.page_counts.clear();
  for (const auto& hit : search_.results) {
    ++search_.page_counts[hit.page];
  }

  if (search_.empty()) {
    search_.current = -1;
    last_action_.set("Pattern not found: {}", search_.term);
    return;
  }

  // Check if any hit is already visible in the viewport
  auto client = frontend_->client_info();
  int vh = client.viewport_pixel().height;
  int first_visible = -1;
  for (int i = 0; i < search_.total(); ++i) {
    const auto& hit = search_.results[i];
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
    search_.current = first_visible;
  }
  else {
    // Find first match on or after current page
    int cur = current_page();
    search_.current = 0;
    for (int i = 0; i < search_.total(); ++i) {
      if (search_.results[i].page >= cur) {
        search_.current = i;
        break;
      }
    }
    scroll_to_search_hit();
  }

  // Wrap refresh + viewport update in BSU/ESU so the image free/upload
  // from refresh_highlights is atomic with show_pages (prevents flash).
  static const char BSU[] = "\033[?2026h";
  static const char ESU[] = "\033[?2026l";
  frontend_->write_raw(BSU, sizeof(BSU) - 1);
  refresh_search_pages();
  update_viewport();
  frontend_->write_raw(ESU, sizeof(ESU) - 1);
}

void App::clear_search() {
  search_.clear();
  static const char BSU[] = "\033[?2026h";
  static const char ESU[] = "\033[?2026l";
  frontend_->write_raw(BSU, sizeof(BSU) - 1);
  refresh_search_pages();
  update_viewport();
  frontend_->write_raw(ESU, sizeof(ESU) - 1);
}

void App::search_navigate(int delta) {
  if (search_.empty()) {
    return;
  }

  int n = search_.total();
  search_.current = ((search_.current + delta) % n + n) % n;
  scroll_to_search_hit();
  update_viewport();
}

void App::scroll_to_search_hit() {
  if (search_.current < 0 || search_.current >= search_.total()) {
    return;
  }

  push_jump_history();
  const auto& hit = search_.results[search_.current];
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
  else if (fg.is_default && detected_terminal_bg_) {
    // Infer fg from bg: dark bg → light fg, light bg → dark fg
    float bg_lum = detected_terminal_bg_->luminance();
    auto level = static_cast<uint8_t>(bg_lum < 0.5f ? 192 + (1.0f - bg_lum) * 63 : static_cast<int>((1.0f - bg_lum) * 0.12f * 255));
    fg = Color::rgb(level, level, level);
  }

  // bg = recolor_light (replaces white/background)
  Color bg = colors_.recolor_light;
  if (bg.is_default && detected_terminal_bg_) {
    bg = *detected_terminal_bg_;
  }
  else if (bg.is_default && detected_terminal_fg_) {
    // Infer bg from fg: light fg → dark bg, dark fg → light bg
    float fg_lum = detected_terminal_fg_->luminance();
    auto level = static_cast<uint8_t>(fg_lum > 0.5f ? (1.0f - fg_lum) * 0.12f * 255 : (1.0f - (1.0f - fg_lum) * 0.12f) * 255);
    bg = Color::rgb(level, level, level);
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
  // Clear any stale hint labels from a previous invocation, then refresh the viewport.
  frontend_->clear_overlay();
  update_viewport();

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

  spdlog::debug("enter_link_hints: pages=[{},{}] scroll=({},{}) vp={}x{}", viewport_first_page_, viewport_last_page_,
                scroll_.x, scroll_.y, vw, vh);

  for (int p = viewport_first_page_; p <= viewport_last_page_; ++p) {
    auto page_links = doc_.load_links(p);
    int visible_count = 0;
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
      ++visible_count;
    }
    spdlog::debug("enter_link_hints: page {} total={} visible={}", p, page_links.size(), visible_count);
  }

  spdlog::debug("enter_link_hints: total visible links={}", link_hints_.size());

  if (link_hints_.empty()) {
    last_action_.set("No links on page");
    return;
  }

  // Assign labels using base-26 encoding (a-z).
  static const char HintChars[] = "asdfghjklqwertyuiopzxcvbnm";
  static const int NumChars = 26;

  // Determine label length needed: ceil(log26(n))
  int label_len = 1;
  {
    int capacity = NumChars;
    while (capacity < static_cast<int>(link_hints_.size())) {
      ++label_len;
      capacity *= NumChars;
    }
  }

  for (int i = 0; i < static_cast<int>(link_hints_.size()); ++i) {
    std::string label;
    int idx = i;
    for (int d = label_len - 1; d >= 0; --d) {
      int divisor = 1;
      for (int k = 0; k < d; ++k) {
        divisor *= NumChars;
      }
      label += HintChars[(idx / divisor) % NumChars];
    }
    link_hints_[i].label = std::move(label);
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
  frontend_->clear_overlay();

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

    update_viewport();
  }
  else {
    // External link — copy to clipboard (works over SSH via OSC 52)
    frontend_->copy_to_clipboard(link.uri);

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
    update_viewport();
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

void App::selection_goto(action::SelectionTarget target) {
  if (app_mode_ != AppMode::Visual && app_mode_ != AppMode::VisualBlock) {
    return;
  }

  PagePoint dest;
  switch (target) {
    case action::SelectionTarget::WordEnd:
      dest = doc_.end_of_word_boundary(selection_extent_);
      break;
    case action::SelectionTarget::LineStart:
      dest = doc_.line_start(selection_extent_);
      break;
    case action::SelectionTarget::LineEnd:
      dest = doc_.line_end(selection_extent_);
      break;
    case action::SelectionTarget::FirstNonSpace:
      dest = doc_.first_non_space(selection_extent_);
      break;
    case action::SelectionTarget::DocStart: {
      // Go to first char on page 0
      auto chars_first = doc_.line_start({0, {}});
      dest = chars_first;
      break;
    }
    case action::SelectionTarget::DocEnd: {
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

void App::refresh_selection_pages() {
  int lo = std::min(selection_anchor_.page, selection_extent_.page);
  int hi = std::max(selection_anchor_.page, selection_extent_.page);
  hi = std::min(hi, static_cast<int>(layout_.size()) - 1);
  page_manager_.refresh_highlights(lo, hi, doc_, *frontend_, make_highlight_params());
}

void App::refresh_search_pages() {
  int num_pages = static_cast<int>(layout_.size());
  if (num_pages > 0) {
    page_manager_.refresh_highlights(0, num_pages - 1, doc_, *frontend_, make_highlight_params());
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
    DocRect sel = DocRect::from_corners(
        std::min(selection_anchor_.pos.x, selection_extent_.pos.x),
        std::min(selection_anchor_.pos.y, selection_extent_.pos.y),
        std::max(selection_anchor_.pos.x, selection_extent_.pos.x),
        std::max(selection_anchor_.pos.y, selection_extent_.pos.y)
    );
    for (int p = lo; p <= hi; ++p) {
      auto page_text = doc_.copy_rect_text(p, sel);
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

  frontend_->copy_to_clipboard(text);

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
      search_.term,
      search_.current >= 0 ? search_.current + 1 : 0,
      search_.total(),
      search_.matches_on_page(current_page()),
      app_mode_ == AppMode::LinkHints,
      std::move(vis_mode),
      {},
      0,
      {},
  };

  if (show_stats_) {
    auto [pages, bytes] = page_manager_.stats();
    vs.cache_pages = std::move(pages);
    vs.cache_bytes = bytes;
  }

  if (last_action_.active()) {
    vs.flash_message = last_action_.text();
  }

  return vs;
}
