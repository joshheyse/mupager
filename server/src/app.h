#pragma once

#include "args.h"
#include "command.h"
#include "document.h"
#include "frontend.h"

#include <cstdint>
#include <memory>
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

  std::unique_ptr<Frontend> frontend_;
  Document doc_;
  bool running_ = true;
  int scroll_x_ = 0;
  int scroll_y_ = 0;
  bool pending_g_ = false;
  int pending_count_ = 0;

  std::vector<PageLayout> layout_;
  std::unordered_map<int, CachedPage> page_cache_;

  int viewport_first_page_ = 0;
  int viewport_last_page_ = 0;
};
