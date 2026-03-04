#pragma once

#include "args.h"
#include "color.h"
#include "color_scheme.h"
#include "command.h"
#include "document.h"
#include "frontend.h"
#include "geometry.h"
#include "terminal/key_bindings.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief Layout information for a single page in the continuous scroll.
struct PageLayout {
  PixelRect rect; ///< Page position and rendered size in global pixel coordinates.
  float zoom;     ///< Width-fit zoom for this page.
};

/// @brief Cached upload state for a rendered page.
struct CachedPage {
  uint32_t image_id;
  PixelSize pixel_size;                   ///< Rendered pixel dimensions.
  CellSize cell_grid;                     ///< Grid dimensions in cells.
  float render_scale;                     ///< Render scale this was rendered at (0 = exact zoom, no viewporting).
  float render_zoom;                      ///< Actual zoom passed to MuPDF (for cache invalidation in NEVER mode).
  size_t memory_bytes;                    ///< Uncompressed pixmap size (w * h * components).
  std::vector<unsigned char> base_pixels; ///< Post-theme, pre-highlight packed pixels (w*h*comp).
  int base_w = 0;                         ///< Width of base_pixels.
  int base_h = 0;                         ///< Height of base_pixels.
  int base_comp = 0;                      ///< Components per pixel of base_pixels.
};

/// @brief View mode for page display.
enum class ViewMode {
  Continuous, ///< Pages flow with gaps between them.
  PageWidth,  ///< One page at a time, fit to width.
  PageHeight, ///< One page at a time, fit to height.
  SideBySide  ///< Two pages displayed side by side.
};

/// @brief Convert ViewMode to its config/RPC string representation.
constexpr const char* to_string(ViewMode m) {
  switch (m) {
    case ViewMode::Continuous:
      return "continuous";
    case ViewMode::PageWidth:
      return "page-width";
    case ViewMode::PageHeight:
      return "page-height";
    case ViewMode::SideBySide:
      return "side-by-side";
  }
  return "";
}

/// @brief Convert ViewMode to its display label.
constexpr const char* to_label(ViewMode m) {
  switch (m) {
    case ViewMode::Continuous:
      return "Continuous";
    case ViewMode::PageWidth:
      return "Page-Width";
    case ViewMode::PageHeight:
      return "Page-Height";
    case ViewMode::SideBySide:
      return "Side-by-Side";
  }
  return "";
}

/// @brief Parse a ViewMode from its string representation.
/// Accepts "page" as alias for "page-width".
/// @return The parsed ViewMode, or nullopt on invalid input.
std::optional<ViewMode> parse_view_mode(const std::string& s);

/// @brief Color theme.
enum class Theme {
  Light,
  Dark,
  Auto,
  Terminal
};

/// @brief Convert Theme to its config/RPC string representation.
constexpr const char* to_string(Theme t) {
  switch (t) {
    case Theme::Light:
      return "light";
    case Theme::Dark:
      return "dark";
    case Theme::Auto:
      return "auto";
    case Theme::Terminal:
      return "terminal";
  }
  return "";
}

/// @brief Convert Theme to its display label (statusline).
const char* to_label(Theme t, Theme effective);

/// @brief Parse a Theme from its string representation.
/// @return The parsed Theme, or nullopt on invalid input.
std::optional<Theme> parse_theme(const std::string& s);

/// @brief Shared application mode (frontend-agnostic).
enum class AppMode {
  Normal,
  Visual,
  VisualBlock,
  LinkHints
};

/// @brief Input mode for terminal key translation (superset of AppMode).
enum class InputMode {
  Normal,
  Command,
  Search,
  Help,
  Outline,
  Sidebar,
  Visual,
  VisualBlock,
  LinkHints
};

/// @brief A saved scroll position for jump history.
struct JumpPoint {
  int scroll_x; ///< Horizontal scroll position.
  int scroll_y; ///< Vertical scroll position.
};

/// @brief An active link hint during LINK_HINTS mode.
struct ActiveLinkHint {
  PageLink link;     ///< The underlying page link.
  std::string label; ///< Assigned hint label.
  int screen_col;    ///< Screen column for label display.
  int screen_row;    ///< Screen row for label display.
};

/// @brief A temporary status message that auto-expires after a fixed duration.
struct FlashMessage {
  static constexpr auto Duration = std::chrono::seconds(1);

  /// @brief Set the flash message text.
  void set(std::string msg) {
    text_ = std::move(msg);
    time_ = std::chrono::steady_clock::now();
  }

  /// @brief Set the flash message with std::format-style arguments.
  template <typename... Args>
  void set(std::format_string<Args...> fmt, Args&&... args) {
    text_ = std::format(fmt, std::forward<Args>(args)...);
    time_ = std::chrono::steady_clock::now();
  }

  /// @brief The raw message text (may be expired).
  const std::string& text() const {
    return text_;
  }

  /// @brief True if a message is set and hasn't expired.
  bool active() const {
    return !text_.empty() && std::chrono::steady_clock::now() - time_ < Duration;
  }

  /// @brief True if a message was set but has expired.
  bool expired() const {
    return !text_.empty() && std::chrono::steady_clock::now() - time_ >= Duration;
  }

  void clear() {
    text_.clear();
  }

private:
  std::string text_;
  std::chrono::steady_clock::time_point time_;
};

/// @brief Serializable view state for RPC notifications.
struct ViewState {
  int current_page; ///< 1-based current page.
  int total_pages;  ///< Total number of pages.
  int zoom_percent; ///< Zoom as percentage (100 = fit-to-width).
  std::string view_mode;
  std::string theme;
  std::string search_term;
  int search_current; ///< 1-based index of current match (0 = none).
  int search_total;   ///< Total number of search matches.
  bool link_hints_active;
  std::string visual_mode;   ///< Visual mode string ("visual", "visual-block", or "").
  std::string cache_pages;   ///< Cached page ranges (e.g. "1-3,5,8-10"), empty when debug is off.
  size_t cache_bytes = 0;    ///< Total cached memory in bytes, 0 when debug is off.
  std::string flash_message; ///< Active flash message text, empty if none/expired.
};

/// @brief Main application controller — pure command processor.
///
/// Commands in, state updates out. No event loop, no input parsing,
/// no knowledge of which frontend is running.
class App {
public:
  /// @brief Construct the application.
  /// @param frontend The display frontend to use.
  /// @param args Parsed command line arguments.
  /// @param detected_fg Detected terminal foreground color (from OSC query).
  /// @param detected_bg Detected terminal background color (from OSC query).
  App(std::unique_ptr<Frontend> frontend, const Args& args, std::optional<Color> detected_fg = std::nullopt, std::optional<Color> detected_bg = std::nullopt);

  /// @brief Perform first render (extracted from old run() preamble).
  void initialize();

  /// @brief Sole entry point for all state mutations.
  void handle_command(const Command& cmd);

  /// @brief Handle idle tasks: pre-upload, flash expiry, resize detection.
  void idle_tick();

  /// @brief Whether the application is still running.
  bool is_running() const {
    return running_;
  }

  /// @brief Current application mode (shared, frontend-agnostic).
  AppMode app_mode() const {
    return app_mode_;
  }

  /// @brief Current input mode (includes terminal-only modes for translate() compatibility).
  InputMode input_mode() const;

  /// @brief Compute the current view state for RPC notifications.
  ViewState view_state() const;

  /// @brief Get the document outline (loads on first call).
  const Outline& outline();

  /// @brief Access the key bindings.
  const KeyBindings& bindings() const {
    return bindings_;
  }

  /// @brief Collect links from currently visible pages.
  std::vector<PageLink> visible_links() const;

private:
  static constexpr int PageGapPx = 16;

  void build_layout();
  int page_at_y(int y) const;
  void update_viewport();
  void ensure_pages_uploaded(int first, int last);
  void evict_distant_pages(int first, int last);
  void pre_upload_adjacent();
  void scroll(int dx, int dy);
  void render();
  void jump_to_page(int page);
  int current_page() const;
  int document_height() const;
  void do_reload();
  void apply_theme(const std::string& name);
  void apply_view_mode(const std::string& name);
  void apply_render_scale(const std::string& name);
  float effective_render_scale() const;
  void handle_zoom_change(float old_zoom);

  void execute_search();
  void clear_search();
  void search_navigate(int delta);
  void scroll_to_search_hit();
  void highlight_page_matches(Pixmap& pixmap, int page_num, float render_zoom);

  void push_jump_history();
  void enter_link_hints();
  void render_link_hints();
  void follow_link(const PageLink& link);
  void exit_link_hints();

  void enter_visual_mode(bool block_mode);
  void update_selection_extent(int col, int row);
  void move_selection_extent(int dx_cells, int dy_cells);
  void move_selection_word(int direction);
  void selection_goto(cmd::SelectionTarget target);
  void yank_selection();
  void cancel_selection();
  void invalidate_selection_pages();
  void refresh_selection_pages();
  void highlight_selection(Pixmap& pixmap, int page_num, float render_zoom);
  PagePoint screen_to_page_point(int col, int row) const;
  void copy_to_clipboard(const std::string& text);

  /// @brief Resolve AUTO theme based on detected terminal bg luminance.
  Theme effective_theme() const;

  /// @brief Resolve recolor colors from config/detected values.
  std::pair<Color, Color> resolve_recolor_colors() const;

  /// @brief Returns a formatted string of cached page numbers and total memory in bytes.
  std::pair<std::string, size_t> cache_stats() const;

  std::unique_ptr<Frontend> frontend_;
  Document doc_;
  ColorScheme colors_;
  KeyBindings bindings_;
  std::optional<Color> detected_terminal_fg_;
  std::optional<Color> detected_terminal_bg_;
  bool running_ = true;
  bool show_stats_ = false;
  int scroll_lines_ = 3;                     ///< Lines per scroll step (from config/CLI).
  size_t max_page_cache_ = 64 * 1024 * 1024; ///< Max page cache size in bytes.
  PixelPoint scroll_;
  ViewMode view_mode_ = ViewMode::Continuous;
  Theme theme_ = Theme::Dark;
  AppMode app_mode_ = AppMode::Normal;
  float user_zoom_ = 1.0f;           ///< User zoom multiplier (1.0 = fit-to-viewport).
  RenderScale render_scale_setting_; ///< From CLI --render-scale.
  std::string search_term_;
  int search_page_matches_ = 0;
  int search_total_matches_ = 0;
  SearchResults search_results_;
  int search_current_ = -1; ///< Index into search_results_ of the focused match (-1 = none).

  std::vector<PageLayout> layout_;
  std::unordered_map<int, CachedPage> page_cache_;

  int viewport_first_page_ = 0;
  int viewport_last_page_ = 0;
  PixelSize layout_size_; ///< Terminal pixel size used in last build_layout().

  Outline outline_;

  std::string file_path_;         ///< Document file path for reload.
  std::string source_path_;       ///< Original source path before conversion. Empty if not converted.
  std::string converter_cmd_;     ///< Resolved converter command for reconversion. Empty if not converted.
  bool watch_ = false;            ///< Auto-reload on file changes.
  std::time_t watched_mtime_ = 0; ///< Last known mtime of watched file.

  std::vector<JumpPoint> jump_history_;
  int jump_index_ = -1; ///< Current position in jump history (-1 = at head).

  std::vector<ActiveLinkHint> link_hints_;
  std::string link_hint_input_;

  PagePoint selection_anchor_{};
  PagePoint selection_extent_{};
  PagePoint last_click_point_{};
  bool has_click_point_ = false;

  FlashMessage last_action_;
  std::chrono::steady_clock::time_point last_activity_time_; ///< Last render or input event, for deferring pre-uploads.
};
