#pragma once

#include <string>
#include <variant>

/// @brief Application command types for dispatch.
namespace cmd {

// Bindable actions have a static constexpr `action` name used by key bindings config.

struct Quit {
  static constexpr const char* Action = "quit";
};

struct Resize {
  int cols;       ///< Window width in columns.
  int rows;       ///< Window height in rows.
  int offset_row; ///< Screen row offset of the Neovim window.
  int offset_col; ///< Screen column offset of the Neovim window.
};

struct ScrollDown {
  static constexpr const char* Action = "scroll-down";
  int count = 1;
};

struct ScrollUp {
  static constexpr const char* Action = "scroll-up";
  int count = 1;
};

struct HalfPageDown {
  static constexpr const char* Action = "half-page-down";
};

struct HalfPageUp {
  static constexpr const char* Action = "half-page-up";
};

struct PageDown {
  static constexpr const char* Action = "page-down";
};

struct PageUp {
  static constexpr const char* Action = "page-up";
};

struct ScrollLeft {
  static constexpr const char* Action = "scroll-left";
  int count = 1;
};

struct ScrollRight {
  static constexpr const char* Action = "scroll-right";
  int count = 1;
};

struct GotoPage {
  int page; ///< 1-based page number.
};

struct GotoFirstPage {
  static constexpr const char* Action = "first-page";
};

struct GotoLastPage {
  static constexpr const char* Action = "last-page";
};

struct ZoomIn {
  static constexpr const char* Action = "zoom-in";
};

struct ZoomOut {
  static constexpr const char* Action = "zoom-out";
};

struct ZoomReset {
  static constexpr const char* Action = "zoom-reset";
};

struct ToggleViewMode {
  static constexpr const char* Action = "toggle-view";
};

struct SetViewMode {
  std::string mode;
};

struct ToggleTheme {
  static constexpr const char* Action = "toggle-theme";
};

struct SetTheme {
  std::string theme;
};

struct SetRenderScale {
  std::string strategy;
};

struct SetSidebarWidth {
  int cols; ///< Sidebar width in columns (0 = default).
};

struct Reload {};

struct Search {
  std::string term;
};

struct SearchNext {
  static constexpr const char* Action = "next-match";
};

struct SearchPrev {
  static constexpr const char* Action = "prev-match";
};

struct ClearSearch {
  static constexpr const char* Action = "clear-search";
};

struct JumpBack {
  static constexpr const char* Action = "jump-back";
};

struct JumpForward {
  static constexpr const char* Action = "jump-forward";
};

struct EnterLinkHints {
  static constexpr const char* Action = "link-hints";
};

struct LinkHintKey {
  char ch;
};

struct LinkHintCancel {};
struct GetOutline {};
struct GetLinks {};
struct GetState {};

// Terminal-only actions (dispatched via key bindings, intercepted by TerminalController)
struct OpenOutline {
  static constexpr const char* Action = "outline";
};

struct ToggleSidebar {
  static constexpr const char* Action = "sidebar";
};

struct EnterCommandMode {
  static constexpr const char* Action = "command-mode";
};

struct EnterSearchMode {
  static constexpr const char* Action = "search";
};

struct ShowHelp {
  static constexpr const char* Action = "help";
};

struct Hide {};
struct Show {};

struct MouseScroll {
  int dx = 0; ///< Horizontal pixel delta.
  int dy = 0; ///< Vertical pixel delta.
};

struct ClickAt {
  int col; ///< Screen column (0-based cell).
  int row; ///< Screen row (0-based cell).
};

struct EnterVisualMode {
  static constexpr const char* Action = "visual-mode";
};

struct EnterVisualBlockMode {
  static constexpr const char* Action = "visual-block-mode";
};

struct SelectionMove {
  int dx; ///< Relative cell movement X.
  int dy; ///< Relative cell movement Y.
};

struct SelectionYank {
  static constexpr const char* Action = "visual-yank";
};

struct SelectionCancel {};

struct SelectionMoveWord {
  int direction; ///< +1 = forward (w), -1 = backward (b).
};

/// @brief Target for selection goto commands.
enum class SelectionTarget {
  LineStart,
  LineEnd,
  FirstNonSpace,
  WordEnd,
  DocStart,
  DocEnd
};

struct SelectionGoto {
  SelectionTarget target;
};

struct DragStart {
  int col; ///< Screen column (0-based cell).
  int row; ///< Screen row (0-based cell).
};

struct DragUpdate {
  int col; ///< Screen column (0-based cell).
  int row; ///< Screen row (0-based cell).
};

struct DragEnd {
  int col; ///< Screen column (0-based cell).
  int row; ///< Screen row (0-based cell).
};

} // namespace cmd

/// @brief Variant type encompassing all application commands.
using Command = std::variant<
    cmd::Quit,
    cmd::Resize,
    cmd::ScrollDown,
    cmd::ScrollUp,
    cmd::HalfPageDown,
    cmd::HalfPageUp,
    cmd::PageDown,
    cmd::PageUp,
    cmd::ScrollLeft,
    cmd::ScrollRight,
    cmd::GotoPage,
    cmd::GotoFirstPage,
    cmd::GotoLastPage,
    cmd::ZoomIn,
    cmd::ZoomOut,
    cmd::ZoomReset,
    cmd::ToggleViewMode,
    cmd::SetViewMode,
    cmd::ToggleTheme,
    cmd::SetTheme,
    cmd::SetRenderScale,
    cmd::SetSidebarWidth,
    cmd::Reload,
    cmd::Search,
    cmd::SearchNext,
    cmd::SearchPrev,
    cmd::ClearSearch,
    cmd::JumpBack,
    cmd::JumpForward,
    cmd::EnterLinkHints,
    cmd::LinkHintKey,
    cmd::LinkHintCancel,
    cmd::GetOutline,
    cmd::GetLinks,
    cmd::GetState,
    cmd::OpenOutline,
    cmd::ToggleSidebar,
    cmd::EnterCommandMode,
    cmd::EnterSearchMode,
    cmd::ShowHelp,
    cmd::Hide,
    cmd::Show,
    cmd::MouseScroll,
    cmd::ClickAt,
    cmd::EnterVisualMode,
    cmd::EnterVisualBlockMode,
    cmd::SelectionMove,
    cmd::SelectionYank,
    cmd::SelectionCancel,
    cmd::SelectionMoveWord,
    cmd::SelectionGoto,
    cmd::DragStart,
    cmd::DragUpdate,
    cmd::DragEnd>;
