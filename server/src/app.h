#pragma once

#include "args.h"
#include "document.h"
#include "frontend.h"
#include "geometry.h"
#include "outline.h"
#include "rpc_command.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
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
  PixelSize pixel_size; ///< Rendered pixel dimensions.
  CellSize cell_grid;   ///< Grid dimensions in cells.
  int oversample;       ///< Oversample factor this was rendered at (0 = exact zoom, no viewporting).
  float render_zoom;    ///< Actual zoom passed to MuPDF (for cache invalidation in NEVER mode).
  size_t memory_bytes;  ///< Uncompressed pixmap size (w * h * components).
};

/// @brief View mode for page display.
enum class ViewMode {
  CONTINUOUS,  ///< Pages flow with gaps between them.
  PAGE_WIDTH,  ///< One page at a time, fit to width.
  PAGE_HEIGHT, ///< One page at a time, fit to height.
  SIDE_BY_SIDE ///< Two pages displayed side by side.
};

/// @brief Color theme.
enum class Theme {
  LIGHT,
  DARK
};

/// @brief Input mode for command/search bar.
enum class InputMode {
  NORMAL,
  COMMAND,
  SEARCH,
  HELP,
  OUTLINE,
  SIDEBAR,
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
  App(std::unique_ptr<Frontend> frontend, const Args& args);

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
  int effective_oversample() const;
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

  /// @brief Returns a formatted string of cached page numbers and total memory in bytes.
  std::pair<std::string, size_t> cache_stats() const;

  std::unique_ptr<Frontend> frontend_;
  Document doc_;
  bool running_ = true;
  bool show_stats_ = false;
  PixelPoint scroll_;
  ViewMode view_mode_ = ViewMode::CONTINUOUS;
  Theme theme_ = Theme::DARK;
  InputMode input_mode_ = InputMode::NORMAL;
  float user_zoom_ = 1.0f;        ///< User zoom multiplier (1.0 = fit-to-viewport).
  Oversample oversample_setting_; ///< From CLI --oversample.
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

  FlashMessage last_action_;
  std::chrono::steady_clock::time_point last_activity_time_; ///< Last render or input event, for deferring pre-uploads.
};
