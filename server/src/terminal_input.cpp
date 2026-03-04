#include "terminal_input.h"

#include "app.h"

#include <algorithm>

/// @brief Simple key-to-command binding.
struct KeyBinding {
  uint32_t key;       ///< Key ID (character code or control code).
  RpcCommand command; ///< Command to dispatch.
  const char* label;  ///< Display label for the help overlay.
};

/// Simple key-to-command bindings (order determines help display order).
static const KeyBinding KEY_BINDINGS[] = {
    {'j', cmd::ScrollDown{}, "j"},     {input::ARROW_DOWN, cmd::ScrollDown{}, "\xe2\x86\x93"},
    {'k', cmd::ScrollUp{}, "k"},       {input::ARROW_UP, cmd::ScrollUp{}, "\xe2\x86\x91"},
    {'d', cmd::HalfPageDown{}, "d"},   {'u', cmd::HalfPageUp{}, "u"},
    {0x06, cmd::PageDown{}, "Ctrl+F"}, {0x02, cmd::PageUp{}, "Ctrl+B"},
    {'h', cmd::ScrollLeft{}, "h"},     {input::ARROW_LEFT, cmd::ScrollLeft{}, "\xe2\x86\x90"},
    {'l', cmd::ScrollRight{}, "l"},    {input::ARROW_RIGHT, cmd::ScrollRight{}, "\xe2\x86\x92"},
    {'+', cmd::ZoomIn{}, "+"},         {'=', cmd::ZoomIn{}, "="},
    {'-', cmd::ZoomOut{}, "-"},        {'0', cmd::ZoomReset{}, "0"},
    {'w', cmd::ZoomReset{}, "w"},      {0x09, cmd::ToggleViewMode{}, "Tab"},
    {'t', cmd::ToggleTheme{}, "t"},    {'q', cmd::Quit{}, "q"},
};

/// Bindings with special dispatch logic (multi-key sequences, modes).
static const HelpBinding SPECIAL_HELP[] = {
    {"gg", "First Page"},
    {"G", "Last Page"},
    {"[n]gg / [n]G", "Go to Page n"},
    {"H", "Jump Back"},
    {"L", "Jump Forward"},
    {"f", "Link Hints"},
    {":", "Command Mode"},
    {":reload", "Reload Document"},
    {"/", "Search"},
    {"n", "Next Match"},
    {"N", "Previous Match"},
    {"o", "Table of Contents"},
    {"e", "Toggle Sidebar TOC"},
    {"?", "Toggle Help"},
};

const std::vector<HelpBinding>& get_help_bindings() {
  static const auto BINDINGS = [] {
    std::vector<HelpBinding> result;
    for (const auto& kb : KEY_BINDINGS) {
      // Find the command description
      const char* desc = "";
      std::visit(
          [&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, cmd::Quit>) {
              desc = "Quit";
            }
            else if constexpr (std::is_same_v<T, cmd::ScrollDown>) {
              desc = "Scroll Down";
            }
            else if constexpr (std::is_same_v<T, cmd::ScrollUp>) {
              desc = "Scroll Up";
            }
            else if constexpr (std::is_same_v<T, cmd::HalfPageDown>) {
              desc = "Half Page Down";
            }
            else if constexpr (std::is_same_v<T, cmd::HalfPageUp>) {
              desc = "Half Page Up";
            }
            else if constexpr (std::is_same_v<T, cmd::GotoFirstPage>) {
              desc = "First Page";
            }
            else if constexpr (std::is_same_v<T, cmd::GotoLastPage>) {
              desc = "Last Page";
            }
            else if constexpr (std::is_same_v<T, cmd::PageDown>) {
              desc = "Page Down";
            }
            else if constexpr (std::is_same_v<T, cmd::PageUp>) {
              desc = "Page Up";
            }
            else if constexpr (std::is_same_v<T, cmd::ScrollLeft>) {
              desc = "Scroll Left";
            }
            else if constexpr (std::is_same_v<T, cmd::ScrollRight>) {
              desc = "Scroll Right";
            }
            else if constexpr (std::is_same_v<T, cmd::ToggleViewMode>) {
              desc = "Toggle View";
            }
            else if constexpr (std::is_same_v<T, cmd::ToggleTheme>) {
              desc = "Toggle Theme";
            }
            else if constexpr (std::is_same_v<T, cmd::ZoomIn>) {
              desc = "Zoom In";
            }
            else if constexpr (std::is_same_v<T, cmd::ZoomOut>) {
              desc = "Zoom Out";
            }
            else if constexpr (std::is_same_v<T, cmd::ZoomReset>) {
              desc = "Fit Width";
            }
            else if constexpr (std::is_same_v<T, cmd::JumpBack>) {
              desc = "Jump Back";
            }
            else if constexpr (std::is_same_v<T, cmd::JumpForward>) {
              desc = "Jump Forward";
            }
            else if constexpr (std::is_same_v<T, cmd::Resize>) {
              desc = "Resize";
            }
          },
          kb.command
      );
      result.push_back({kb.label, desc});
    }
    for (const auto& he : SPECIAL_HELP) {
      result.push_back(he);
    }
    return result;
  }();
  return BINDINGS;
}

std::optional<RpcCommand> TerminalInputHandler::translate(const InputEvent& event, InputMode mode, int terminal_rows, CellSize cell) {
  // Handle mouse events before mode dispatch
  if (event.id == input::MOUSE_SCROLL_UP || event.id == input::MOUSE_SCROLL_DN) {
    int sign = (event.id == input::MOUSE_SCROLL_UP) ? -1 : 1;
    if (event.modifiers & input::MOD_CTRL) {
      return (sign < 0) ? RpcCommand{cmd::ZoomIn{}} : RpcCommand{cmd::ZoomOut{}};
    }
    int step = 3;
    if (event.modifiers & input::MOD_SHIFT) {
      return cmd::MouseScroll{sign * cell.width * step, 0};
    }
    return cmd::MouseScroll{0, sign * cell.height * step};
  }
  if (event.id == input::MOUSE_PRESS) {
    return cmd::ClickAt{event.mouse_col, event.mouse_row};
  }
  if (event.id == input::MOUSE_RELEASE) {
    return std::nullopt;
  }

  bool is_press = event.type == EventType::PRESS || event.type == EventType::UNKNOWN;
  if (!is_press) {
    return std::nullopt;
  }

  // Dismiss help overlay on any key
  if (mode == InputMode::HELP) {
    return cmd::DismissOverlay{};
  }

  // Outline mode input
  if (mode == InputMode::OUTLINE) {
    if (event.id == 27 || event.id == 'q') {
      return cmd::CloseOutline{};
    }
    if (event.id == 0x0E || event.id == input::ARROW_DOWN) {
      return cmd::OutlineNavigate{1};
    }
    if (event.id == 0x10 || event.id == input::ARROW_UP) {
      return cmd::OutlineNavigate{-1};
    }
    if (event.id == input::PAGE_DOWN) {
      int page_size = std::max(1, terminal_rows * 3 / 4 - 4);
      return cmd::OutlineNavigate{page_size};
    }
    if (event.id == input::PAGE_UP) {
      int page_size = std::max(1, terminal_rows * 3 / 4 - 4);
      return cmd::OutlineNavigate{-page_size};
    }
    if (event.id == input::HOME) {
      return cmd::OutlineNavigate{-10000};
    }
    if (event.id == input::END) {
      return cmd::OutlineNavigate{10000};
    }
    if (event.id == '\n' || event.id == '\r') {
      return cmd::OutlineJump{};
    }
    if (event.id == 127 || event.id == 8 || event.id == input::BACKSPACE) {
      return cmd::OutlineFilterBackspace{};
    }
    if (event.id >= 32 && event.id < 127) {
      return cmd::OutlineFilterChar{static_cast<char>(event.id)};
    }
    return std::nullopt;
  }

  // Sidebar mode input
  if (mode == InputMode::SIDEBAR) {
    if (event.id == 27) {
      return cmd::SidebarUnfocus{};
    }
    if (event.id == 'q' || event.id == 'e') {
      return cmd::SidebarClose{};
    }
    if (event.id == 0x0E || event.id == input::ARROW_DOWN) {
      return cmd::SidebarNavigate{1};
    }
    if (event.id == 0x10 || event.id == input::ARROW_UP) {
      return cmd::SidebarNavigate{-1};
    }
    if (event.id == input::PAGE_DOWN) {
      int page_size = std::max(1, terminal_rows - 3);
      return cmd::SidebarNavigate{page_size};
    }
    if (event.id == input::PAGE_UP) {
      int page_size = std::max(1, terminal_rows - 3);
      return cmd::SidebarNavigate{-page_size};
    }
    if (event.id == input::HOME) {
      return cmd::SidebarNavigate{-10000};
    }
    if (event.id == input::END) {
      return cmd::SidebarNavigate{10000};
    }
    if (event.id == '\n' || event.id == '\r') {
      return cmd::SidebarJump{};
    }
    if (event.id == 127 || event.id == 8 || event.id == input::BACKSPACE) {
      return cmd::SidebarFilterBackspace{};
    }
    if (event.id >= 32 && event.id < 127) {
      return cmd::SidebarFilterChar{static_cast<char>(event.id)};
    }
    return std::nullopt;
  }

  // Link hints mode input
  if (mode == InputMode::LINK_HINTS) {
    if (event.id == 27) {
      return cmd::LinkHintCancel{};
    }
    if (event.id >= 'a' && event.id <= 'z') {
      return cmd::LinkHintKey{static_cast<char>(event.id)};
    }
    return std::nullopt;
  }

  // Command mode text input
  if (mode == InputMode::COMMAND) {
    if (event.id == 27) {
      return cmd::CommandCancel{};
    }
    if (event.id == '\n' || event.id == '\r') {
      return cmd::CommandExecute{};
    }
    if (event.id == 127 || event.id == 8 || event.id == input::BACKSPACE) {
      return cmd::CommandBackspace{};
    }
    if (event.id >= 32 && event.id < 127) {
      return cmd::CommandChar{static_cast<char>(event.id)};
    }
    return std::nullopt;
  }

  // Search mode text input
  if (mode == InputMode::SEARCH) {
    if (event.id == 27) {
      return cmd::SearchCancel{};
    }
    if (event.id == '\n' || event.id == '\r') {
      return cmd::SearchExecute{};
    }
    if (event.id == 127 || event.id == 8 || event.id == input::BACKSPACE) {
      return cmd::SearchBackspace{};
    }
    if (event.id >= 32 && event.id < 127) {
      return cmd::SearchChar{static_cast<char>(event.id)};
    }
    return std::nullopt;
  }

  // --- NORMAL mode ---

  // Enter command mode on ':'
  if (event.id == ':') {
    return cmd::EnterCommandMode{};
  }

  // Enter search mode on '/'
  if (event.id == '/') {
    return cmd::EnterSearchMode{};
  }

  // Digits accumulate a count prefix (like vim)
  if (event.id >= '1' && event.id <= '9') {
    pending_count_ = pending_count_ * 10 + static_cast<int>(event.id - '0');
    pending_g_ = false;
    return std::nullopt;
  }
  if (event.id == '0' && pending_count_ > 0) {
    pending_count_ = pending_count_ * 10;
    pending_g_ = false;
    return std::nullopt;
  }

  // Handle gg / {count}gg
  if (event.id == 'g' && event.modifiers == 0) {
    if (pending_g_) {
      pending_g_ = false;
      if (pending_count_ > 0) {
        int page = pending_count_;
        pending_count_ = 0;
        return cmd::GotoPage{page};
      }
      return cmd::GotoFirstPage{};
    }
    pending_g_ = true;
    return std::nullopt;
  }

  // Handle G / {count}G
  if (event.id == 'G' && event.modifiers == 0) {
    if (pending_count_ > 0) {
      int page = pending_count_;
      pending_count_ = 0;
      pending_g_ = false;
      return cmd::GotoPage{page};
    }
    pending_g_ = false;
    return cmd::GotoLastPage{};
  }

  // Any other key resets pending state
  pending_g_ = false;
  pending_count_ = 0;

  // Dispatch simple key bindings from the table
  for (const auto& kb : KEY_BINDINGS) {
    if (event.id == kb.key) {
      return kb.command;
    }
  }

  // Special bindings not in the table
  if (event.id == '?') {
    return cmd::ShowHelp{};
  }
  if (event.id == 'o') {
    return cmd::OpenOutline{};
  }
  if (event.id == 'e') {
    return cmd::ToggleSidebar{};
  }
  if (event.id == 'H') {
    return cmd::JumpBack{};
  }
  if (event.id == 'L') {
    return cmd::JumpForward{};
  }
  if (event.id == 'f') {
    return cmd::EnterLinkHints{};
  }
  if (event.id == 'n') {
    return cmd::SearchNext{};
  }
  if (event.id == 'N') {
    return cmd::SearchPrev{};
  }
  if (event.id == 27) {
    return cmd::ClearSearch{};
  }
  if (event.id == input::RESIZE) {
    return cmd::Resize{};
  }

  return std::nullopt;
}
