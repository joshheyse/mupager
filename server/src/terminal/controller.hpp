#pragma once

#include "terminal/frontend.hpp"
#include "terminal/input.hpp"

#include <string>
#include <vector>

class App;

/// @brief Tracks state and reports whether it changed since last check.
template <typename T>
struct DirtyState {
  /// @brief Update the stored state. Returns true if the value changed.
  bool update(const T& new_value) {
    if (value == new_value) {
      return false;
    }
    value = new_value;
    return true;
  }

  T value{};
};

/// @brief Cached sidebar content for dirty-checking.
struct SidebarState {
  std::vector<std::string> lines;
  int highlight_line = -1;
  int width_cols = 0;
  bool focused = false;
  bool operator==(const SidebarState&) const = default;
};

/// @brief Terminal-only mode (superset of App modes, for terminal UI state).
enum class TerminalMode {
  Normal,
  Command,
  Search,
  Help,
  Outline,
  Sidebar
};

/// @brief Owns all terminal-specific UI: statusline, command bar, search bar,
/// help overlay, outline popup, and sidebar. Forwards shared Commands to App.
class TerminalController {
public:
  TerminalController(App& app, TerminalFrontend& frontend, const KeyBindings& bindings, int scroll_lines);

  /// @brief Process an input event. Handles terminal UI or forwards to App.
  void handle_input(const InputEvent& event);

  /// @brief Idle tick for flash expiry, etc.
  void idle_tick();

  /// @brief Initial UI setup after app.initialize().
  void initialize();

private:
  /// @brief Compute the effective InputMode for translate() from terminal + app modes.
  InputMode effective_input_mode() const;

  /// @brief Forward a shared Action to App and update terminal UI after.
  void forward_action(const Action& act);

  // Terminal UI rendering
  void update_statusline();
  void update_sidebar_display();
  void update_terminal_ui();

  // Command bar
  void enter_command_mode();
  void command_char(char ch);
  void command_backspace();
  void command_execute();
  void command_cancel();

  // Search bar
  void enter_search_mode();
  void search_char(char ch);
  void search_backspace();
  void search_execute();
  void search_cancel();

  // Help
  void show_help();
  void dismiss_overlay();

  // Outline popup
  void open_outline();
  void outline_navigate(int delta);
  void outline_filter_char(char ch);
  void outline_filter_backspace();
  void outline_jump();
  void close_outline();
  void show_outline_popup();
  void outline_apply_filter();

  // Sidebar
  void toggle_sidebar();
  void sidebar_unfocus();
  void sidebar_close();
  void sidebar_navigate(int delta);
  void sidebar_filter_char(char ch);
  void sidebar_filter_backspace();
  void sidebar_jump();
  void sidebar_apply_filter();
  int sidebar_effective_width() const;
  int active_outline_index() const;

  static bool fuzzy_match(const std::string& text, const std::string& pattern);

  /// @brief Parse a command-bar input string into an Action.
  static std::pair<std::optional<Action>, std::string> parse_command_string(const std::string& raw);

  App& app_;
  TerminalFrontend& frontend_;
  TerminalInputHandler input_handler_;
  TerminalMode terminal_mode_ = TerminalMode::Normal;
  DirtyState<SidebarState> sidebar_state_;

  // Command bar
  std::string command_input_;

  // Search bar (in-progress, pre-execute)
  std::string search_input_;

  // Outline popup
  int outline_cursor_ = 0;
  int outline_scroll_ = 0;
  std::string outline_filter_;
  std::vector<int> filtered_indices_;

  // Sidebar
  bool sidebar_visible_ = false;
  int sidebar_width_cols_ = 0;
  int sidebar_cursor_ = 0;
  int sidebar_scroll_ = 0;
  std::string sidebar_filter_;
  std::vector<int> sidebar_filtered_;
};
