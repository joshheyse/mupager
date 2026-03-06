#pragma once

#include <string>
#include <variant>

/// @brief Application action types for dispatch.
namespace action {

struct Quit {
  static constexpr const char* Name = "quit";
  static constexpr const char* Description = "Quit";
};

struct Resize {
  static constexpr const char* Name = "resize";
  int cols;       ///< Window width in columns.
  int rows;       ///< Window height in rows.
  int offset_row; ///< Screen row offset of the Neovim window.
  int offset_col; ///< Screen column offset of the Neovim window.
};

struct ScrollDown {
  static constexpr const char* Name = "scroll_down";
  static constexpr const char* Description = "Scroll Down";
  int count = 1;
};

struct ScrollUp {
  static constexpr const char* Name = "scroll_up";
  static constexpr const char* Description = "Scroll Up";
  int count = 1;
};

struct HalfPageDown {
  static constexpr const char* Name = "half_page_down";
  static constexpr const char* Description = "Half Page Down";
};

struct HalfPageUp {
  static constexpr const char* Name = "half_page_up";
  static constexpr const char* Description = "Half Page Up";
};

struct PageDown {
  static constexpr const char* Name = "page_down";
  static constexpr const char* Description = "Page Down";
};

struct PageUp {
  static constexpr const char* Name = "page_up";
  static constexpr const char* Description = "Page Up";
};

struct ScrollLeft {
  static constexpr const char* Name = "scroll_left";
  static constexpr const char* Description = "Scroll Left";
  int count = 1;
};

struct ScrollRight {
  static constexpr const char* Name = "scroll_right";
  static constexpr const char* Description = "Scroll Right";
  int count = 1;
};

struct GotoPage {
  static constexpr const char* Name = "goto_page";
  int page; ///< 1-based page number.
};

struct GotoFirstPage {
  static constexpr const char* Name = "first_page";
  static constexpr const char* Description = "First Page";
};

struct GotoLastPage {
  static constexpr const char* Name = "last_page";
  static constexpr const char* Description = "Last Page";
};

struct ZoomIn {
  static constexpr const char* Name = "zoom_in";
  static constexpr const char* Description = "Zoom In";
};

struct ZoomOut {
  static constexpr const char* Name = "zoom_out";
  static constexpr const char* Description = "Zoom Out";
};

struct ZoomReset {
  static constexpr const char* Name = "zoom_reset";
  static constexpr const char* Description = "Fit Width";
};

struct ToggleViewMode {
  static constexpr const char* Name = "toggle_view";
  static constexpr const char* Description = "Toggle View";
};

struct SetViewMode {
  static constexpr const char* Name = "set_view_mode";
  std::string mode;
};

struct ToggleTheme {
  static constexpr const char* Name = "toggle_theme";
  static constexpr const char* Description = "Toggle Theme";
};

struct SetTheme {
  static constexpr const char* Name = "set_theme";
  std::string theme;
};

struct SetRenderScale {
  static constexpr const char* Name = "set_render_scale";
  std::string strategy;
};

struct SetSidebarWidth {
  static constexpr const char* Name = "set_sidebar_width";
  int cols; ///< Sidebar width in columns (0 = default).
};

struct Reload {
  static constexpr const char* Name = "reload";
};

struct Search {
  static constexpr const char* Name = "search";
  std::string term;
};

struct SearchNext {
  static constexpr const char* Name = "next_match";
  static constexpr const char* Description = "Next Match";
};

struct SearchPrev {
  static constexpr const char* Name = "prev_match";
  static constexpr const char* Description = "Previous Match";
};

struct ClearSearch {
  static constexpr const char* Name = "clear_search";
  static constexpr const char* Description = "Clear Search";
};

struct JumpBack {
  static constexpr const char* Name = "jump_back";
  static constexpr const char* Description = "Jump Back";
};

struct JumpForward {
  static constexpr const char* Name = "jump_forward";
  static constexpr const char* Description = "Jump Forward";
};

struct EnterLinkHints {
  static constexpr const char* Name = "link_hints";
  static constexpr const char* Description = "Link Hints";
};

struct LinkHintKey {
  static constexpr const char* Name = "link_hint_key";
  char ch;
};

struct LinkHintCancel {
  static constexpr const char* Name = "link_hint_cancel";
};
struct GetOutline {
  static constexpr const char* Name = "get_outline";
};
struct GetLinks {
  static constexpr const char* Name = "get_links";
};
struct GetState {
  static constexpr const char* Name = "get_state";
};

struct OpenOutline {
  static constexpr const char* Name = "outline";
  static constexpr const char* Description = "Table of Contents";
};

struct ToggleSidebar {
  static constexpr const char* Name = "sidebar";
  static constexpr const char* Description = "Toggle Sidebar TOC";
};

struct EnterCommandMode {
  static constexpr const char* Name = "command_mode";
  static constexpr const char* Description = "Command Mode";
};

struct EnterSearchMode {
  static constexpr const char* Name = "enter_search";
  static constexpr const char* Description = "Search";
};

struct ShowHelp {
  static constexpr const char* Name = "help";
  static constexpr const char* Description = "Toggle Help";
};

struct Hide {
  static constexpr const char* Name = "hide";
};
struct Show {
  static constexpr const char* Name = "show";
};

struct MouseScroll {
  static constexpr const char* Name = "mouse_scroll";
  int dx = 0; ///< Horizontal pixel delta.
  int dy = 0; ///< Vertical pixel delta.
};

struct ClickAt {
  static constexpr const char* Name = "click_at";
  int col; ///< Screen column (0-based cell).
  int row; ///< Screen row (0-based cell).
};

struct EnterVisualMode {
  static constexpr const char* Name = "visual_mode";
  static constexpr const char* Description = "Visual Select";
};

struct EnterVisualBlockMode {
  static constexpr const char* Name = "visual_block_mode";
  static constexpr const char* Description = "Visual Block Select";
};

struct SelectionMove {
  static constexpr const char* Name = "selection_move";
  int dx; ///< Relative cell movement X.
  int dy; ///< Relative cell movement Y.
};

struct SelectionYank {
  static constexpr const char* Name = "visual_yank";
  static constexpr const char* Description = "Yank Selection";
};

struct SelectionCancel {
  static constexpr const char* Name = "selection_cancel";
};

struct SelectionMoveWord {
  static constexpr const char* Name = "selection_move_word";
  int direction; ///< +1 = forward (w), -1 = backward (b).
};

/// @brief Target for selection goto actions.
enum class SelectionTarget {
  LineStart,
  LineEnd,
  FirstNonSpace,
  WordEnd,
  DocStart,
  DocEnd
};

struct SelectionGoto {
  static constexpr const char* Name = "selection_goto";
  SelectionTarget target;
};

struct DragStart {
  static constexpr const char* Name = "drag_start";
  int col; ///< Screen column (0-based cell).
  int row; ///< Screen row (0-based cell).
};

struct DragUpdate {
  static constexpr const char* Name = "drag_update";
  int col; ///< Screen column (0-based cell).
  int row; ///< Screen row (0-based cell).
};

struct DragEnd {
  static constexpr const char* Name = "drag_end";
  int col; ///< Screen column (0-based cell).
  int row; ///< Screen row (0-based cell).
};

} // namespace action

/// @brief Variant type encompassing all application actions.
using Action = std::variant<
    action::Quit,
    action::Resize,
    action::ScrollDown,
    action::ScrollUp,
    action::HalfPageDown,
    action::HalfPageUp,
    action::PageDown,
    action::PageUp,
    action::ScrollLeft,
    action::ScrollRight,
    action::GotoPage,
    action::GotoFirstPage,
    action::GotoLastPage,
    action::ZoomIn,
    action::ZoomOut,
    action::ZoomReset,
    action::ToggleViewMode,
    action::SetViewMode,
    action::ToggleTheme,
    action::SetTheme,
    action::SetRenderScale,
    action::SetSidebarWidth,
    action::Reload,
    action::Search,
    action::SearchNext,
    action::SearchPrev,
    action::ClearSearch,
    action::JumpBack,
    action::JumpForward,
    action::EnterLinkHints,
    action::LinkHintKey,
    action::LinkHintCancel,
    action::GetOutline,
    action::GetLinks,
    action::GetState,
    action::OpenOutline,
    action::ToggleSidebar,
    action::EnterCommandMode,
    action::EnterSearchMode,
    action::ShowHelp,
    action::Hide,
    action::Show,
    action::MouseScroll,
    action::ClickAt,
    action::EnterVisualMode,
    action::EnterVisualBlockMode,
    action::SelectionMove,
    action::SelectionYank,
    action::SelectionCancel,
    action::SelectionMoveWord,
    action::SelectionGoto,
    action::DragStart,
    action::DragUpdate,
    action::DragEnd>;
