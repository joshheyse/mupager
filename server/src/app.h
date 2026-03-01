#pragma once

#include "args.h"
#include "command.h"
#include "document.h"
#include "frontend.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief Layout information for a single page in the continuous scroll.
struct PageLayout {
  int global_y;     ///< Where this page starts in global pixel coordinates.
  int pixel_height; ///< Rendered height at current zoom.
  float zoom;       ///< Width-fit zoom for this page.
};

/// @brief Cached upload state for a rendered page.
struct CachedPage {
  uint32_t image_id;
  int pixel_width, pixel_height;
  int cell_cols, cell_rows;
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
  SEARCH
};

/// @brief Main application controller.
class App {
public:
  /// @brief Construct the application.
  /// @param frontend The display frontend to use.
  /// @param args Parsed command line arguments.
  App(std::unique_ptr<Frontend> frontend, const Args& args);

  /// @brief Run the main event loop until quit.
  void run();

private:
  static constexpr int PAGE_GAP_PX = 16;

  void handle_command(Command cmd);
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
  int viewport_height();
  void update_statusline();

  std::unique_ptr<Frontend> frontend_;
  Document doc_;
  bool running_ = true;
  int scroll_x_ = 0;
  int scroll_y_ = 0;
  bool pending_g_ = false;
  int pending_count_ = 0;
  ViewMode view_mode_ = ViewMode::CONTINUOUS;
  Theme theme_ = Theme::DARK;
  InputMode input_mode_ = InputMode::NORMAL;
  std::string search_term_;
  std::string command_input_;
  int search_page_matches_ = 0;
  int search_total_matches_ = 0;

  std::vector<PageLayout> layout_;
  std::unordered_map<int, CachedPage> page_cache_;

  int viewport_first_page_ = 0;
  int viewport_last_page_ = 0;
  unsigned layout_pxy_ = 0; ///< Pixel height used in last build_layout().
  unsigned layout_pxx_ = 0; ///< Pixel width used in last build_layout().
};
