#pragma once

#include <string>
#include <variant>

/// @brief RPC command types sent from the Neovim plugin to the server.
namespace cmd {

struct Quit {};

struct Resize {
  int cols;       ///< Window width in columns.
  int rows;       ///< Window height in rows.
  int offset_row; ///< Screen row offset of the Neovim window.
  int offset_col; ///< Screen column offset of the Neovim window.
};

struct ScrollDown {
  int count = 1;
};

struct ScrollUp {
  int count = 1;
};

struct HalfPageDown {};
struct HalfPageUp {};
struct PageDown {};
struct PageUp {};

struct ScrollLeft {
  int count = 1;
};

struct ScrollRight {
  int count = 1;
};

struct GotoPage {
  int page; ///< 1-based page number.
};

struct GotoFirstPage {};
struct GotoLastPage {};
struct ZoomIn {};
struct ZoomOut {};
struct ZoomReset {};
struct ToggleViewMode {};

struct SetViewMode {
  std::string mode;
};

struct ToggleTheme {};

struct SetTheme {
  std::string theme;
};

struct SetOversample {
  std::string strategy;
};

struct Reload {};

struct Search {
  std::string term;
};

struct SearchNext {};
struct SearchPrev {};
struct ClearSearch {};
struct JumpBack {};
struct JumpForward {};
struct EnterLinkHints {};

struct LinkHintKey {
  char ch;
};

struct LinkHintCancel {};
struct GetOutline {};
struct GetLinks {};
struct GetState {};

// Outline popup
struct OpenOutline {};
struct OutlineNavigate {
  int delta;
};
struct OutlineFilterChar {
  char ch;
};
struct OutlineFilterBackspace {};
struct OutlineJump {};
struct CloseOutline {};

// Sidebar
struct ToggleSidebar {};
struct SidebarUnfocus {};
struct SidebarClose {};
struct SidebarNavigate {
  int delta;
};
struct SidebarFilterChar {
  char ch;
};
struct SidebarFilterBackspace {};
struct SidebarJump {};

// Command mode
struct EnterCommandMode {};
struct CommandChar {
  char ch;
};
struct CommandBackspace {};
struct CommandExecute {};
struct CommandCancel {};

// Search mode
struct EnterSearchMode {};
struct SearchChar {
  char ch;
};
struct SearchBackspace {};
struct SearchExecute {};
struct SearchCancel {};

// Help
struct ShowHelp {};
struct DismissOverlay {};

} // namespace cmd

/// @brief Variant type encompassing all RPC commands.
using RpcCommand = std::variant<
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
    cmd::SetOversample,
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
    cmd::OutlineNavigate,
    cmd::OutlineFilterChar,
    cmd::OutlineFilterBackspace,
    cmd::OutlineJump,
    cmd::CloseOutline,
    cmd::ToggleSidebar,
    cmd::SidebarUnfocus,
    cmd::SidebarClose,
    cmd::SidebarNavigate,
    cmd::SidebarFilterChar,
    cmd::SidebarFilterBackspace,
    cmd::SidebarJump,
    cmd::EnterCommandMode,
    cmd::CommandChar,
    cmd::CommandBackspace,
    cmd::CommandExecute,
    cmd::CommandCancel,
    cmd::EnterSearchMode,
    cmd::SearchChar,
    cmd::SearchBackspace,
    cmd::SearchExecute,
    cmd::SearchCancel,
    cmd::ShowHelp,
    cmd::DismissOverlay>;
