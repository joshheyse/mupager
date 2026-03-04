#include "terminal_input.h"

#include "app.h"

#include <algorithm>

std::optional<RpcCommand> TerminalInputHandler::translate(const InputEvent& event, InputMode mode, int terminal_rows, CellSize cell) {
  // Handle mouse events before mode dispatch
  if (event.id == input::MOUSE_SCROLL_UP || event.id == input::MOUSE_SCROLL_DN) {
    int sign = (event.id == input::MOUSE_SCROLL_UP) ? -1 : 1;
    if (event.modifiers & input::MOD_CTRL) {
      return (sign < 0) ? RpcCommand{cmd::ZoomIn{}} : RpcCommand{cmd::ZoomOut{}};
    }
    if (event.modifiers & input::MOD_SHIFT) {
      return cmd::MouseScroll{sign * cell.width * scroll_lines_, 0};
    }
    return cmd::MouseScroll{0, sign * cell.height * scroll_lines_};
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

  // Digits accumulate a count prefix (like vim)
  if (event.id >= '1' && event.id <= '9') {
    pending_count_ = pending_count_ * 10 + static_cast<int>(event.id - '0');
    pending_prefix_ = false;
    return std::nullopt;
  }
  if (event.id == '0' && pending_count_ > 0) {
    pending_count_ = pending_count_ * 10;
    pending_prefix_ = false;
    return std::nullopt;
  }

  // Handle sequence prefix (e.g. 'g' for gg / {count}gg)
  uint32_t prefix = bindings_.sequence_prefix_key();
  if (prefix != 0 && event.id == prefix && event.modifiers == 0) {
    if (pending_prefix_) {
      pending_prefix_ = false;
      if (pending_count_ > 0) {
        int page = pending_count_;
        pending_count_ = 0;
        return cmd::GotoPage{page};
      }
      auto seq_action = bindings_.sequence_double_action();
      if (seq_action) {
        return action_to_command(*seq_action);
      }
      return std::nullopt;
    }
    pending_prefix_ = true;
    return std::nullopt;
  }

  // Handle LAST_PAGE action specially for count-prefix goto-page
  auto action = bindings_.lookup(event.id);
  if (action == Action::LAST_PAGE && event.modifiers == 0) {
    if (pending_count_ > 0) {
      int page = pending_count_;
      pending_count_ = 0;
      pending_prefix_ = false;
      return cmd::GotoPage{page};
    }
    pending_prefix_ = false;
    pending_count_ = 0;
    return cmd::GotoLastPage{};
  }

  // Any other key resets pending state
  pending_prefix_ = false;
  pending_count_ = 0;

  // Dispatch via key bindings
  if (action) {
    return action_to_command(*action);
  }

  // Resize is not configurable
  if (event.id == input::RESIZE) {
    return cmd::Resize{};
  }

  return std::nullopt;
}
