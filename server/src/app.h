#pragma once

#include "args.h"
#include "color.h"
#include "color_scheme.h"
#include "document.h"
#include "frontend.h"
#include "geometry.h"
#include "key_bindings.h"
#include "outline.h"
#include "rpc_command.h"

#include <chrono>
#include <cstdint>
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
  CONTINUOUS,  ///< Pages flow with gaps between them.
  PAGE_WIDTH,  ///< One page at a time, fit to width.
  PAGE_HEIGHT, ///< One page at a time, fit to height.
  SIDE_BY_SIDE ///< Two pages displayed side by side.
};

/// @brief Convert ViewMode to its config/RPC string representation.
constexpr const char* to_string(ViewMode m) {
  switch (m) {
    case ViewMode::CONTINUOUS:
      return "continuous";
    case ViewMode::PAGE_WIDTH:
      return "page-width";
    case ViewMode::PAGE_HEIGHT:
      return "page-height";
    case ViewMode::SIDE_BY_SIDE:
      return "side-by-side";
  }
  return "";
}

/// @brief Convert ViewMode to its display label.
constexpr const char* to_label(ViewMode m) {
  switch (m) {
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
}

/// @brief Parse a ViewMode from its string representation.
/// Accepts "page" as alias for "page-width".
/// @return The parsed ViewMode, or nullopt on invalid input.
std::optional<ViewMode> parse_view_mode(const std::string& s);

/// @brief Color theme.
enum class Theme {
  LIGHT,
  DARK,
  AUTO,
  TERMINAL
};

/// @brief Convert Theme to its config/RPC string representation.
constexpr const char* to_string(Theme t) {
  switch (t) {
    case Theme::LIGHT:
      return "light";
    case Theme::DARK:
      return "dark";
    case Theme::AUTO:
      return "auto";
    case Theme::TERMINAL:
      return "terminal";
  }
  return "";
}

/// @brief Convert Theme to its display label (statusline).
const char* to_label(Theme t, Theme effective);

/// @brief Parse a Theme from its string representation.
/// @return The parsed Theme, or nullopt on invalid input.
std::optional<Theme> parse_theme(const std::string& s);

/// @brief Input mode for command/search bar.
enum class InputMode {
  NORMAL,
  COMMAND,
  SEARCH,
  HELP,
  OUTLINE,
  SIDEBAR,
  VISUAL,
  VISUAL_BLOCK,
  LINK_HINTS
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
  static constexpr auto DURATION = std::chrono::seconds(1);

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
    return !text_.empty() && std::chrono::steady_clock::now() - time_ < DURATION;
  }

  /// @brief True if a message was set but has expired.
  bool expired() const {
    return !text_.empty() && std::chrono::steady_clock::now() - time_ >= DURATION;
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
  std::string visual_mode; ///< Visual mode string ("visual", "visual-block", or "").
  std::string cache_pages; ///< Cached page ranges (e.g. "1-3,5,8-10"), empty when debug is off.
  size_t cache_bytes = 0;  ///< Total cached memory in bytes, 0 when debug is off.
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
  void handle_command(const RpcCommand& cmd);

  /// @brief Handle idle tasks: pre-upload, flash expiry, resize detection.
  void idle_tick();

  /// @brief Whether the application is still running.
  bool is_running() const {
    return running_;
  }

  /// @brief Current input mode.
  InputMode input_mode() const {
    return input_mode_;
  }

  /// @brief Compute the current view state for RPC notifications.
  ViewState view_state() const;

  /// @brief Get the document outline (loads on first call).
  const Outline& outline();

  /// @brief Collect links from currently visible pages.
  std::vector<PageLink> visible_links() const;

private:
  static constexpr int PAGE_GAP_PX = 16;

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
  void update_statusline();
  void show_help();
  void execute_command();
  float effective_render_scale() const;
  void handle_zoom_change(float old_zoom);
  void show_outline_popup();
  void outline_apply_filter();
  void outline_navigate(int delta);
  void outline_jump();
  static bool fuzzy_match(const std::string& text, const std::string& pattern);

  int sidebar_effective_width() const;
  int active_outline_index() const;
  void update_sidebar();
  void sidebar_apply_filter();
  void sidebar_navigate(int delta);
  void sidebar_jump();

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
  ViewMode view_mode_ = ViewMode::CONTINUOUS;
  Theme theme_ = Theme::DARK;
  InputMode input_mode_ = InputMode::NORMAL;
  float user_zoom_ = 1.0f;           ///< User zoom multiplier (1.0 = fit-to-viewport).
  RenderScale render_scale_setting_; ///< From CLI --render-scale.
  std::string search_term_;
  std::string command_input_;
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
  std::vector<int> filtered_indices_;
  int outline_cursor_ = 0;
  int outline_scroll_ = 0;
  std::string outline_filter_;

  bool sidebar_visible_ = false;
  int sidebar_width_cols_ = 0; ///< 0 = use default 20%.
  int sidebar_cursor_ = 0;
  int sidebar_scroll_ = 0;
  std::string sidebar_filter_;
  std::vector<int> sidebar_filtered_;

  std::string file_path_; ///< Document file path for reload.

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
