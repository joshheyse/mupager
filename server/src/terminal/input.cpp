#include "terminal/input.hpp"

#include "app.hpp"
#include "command.hpp"
#include "input_event.hpp"
#include "geometry.hpp"

#include <optional>
#include <cstdint>

std::optional<Command> TerminalInputHandler::translate(const InputEvent& event, InputMode mode, int /*terminal_rows*/, CellSize cell) {
  // Handle mouse events before mode dispatch
  if (event.id == input::MouseScrollUp || event.id == input::MouseScrollDn) {
    int sign = (event.id == input::MouseScrollUp) ? -1 : 1;
    if (event.modifiers & input::ModCtrl) {
      return (sign < 0) ? Command{cmd::ZoomIn{}} : Command{cmd::ZoomOut{}};
    }
    if (event.modifiers & input::ModShift) {
      return cmd::MouseScroll{sign * cell.width * scroll_lines_, 0};
    }
    return cmd::MouseScroll{0, sign * cell.height * scroll_lines_};
  }
  if (event.id == input::MousePress) {
    if (mode == InputMode::Visual || mode == InputMode::VisualBlock) {
      return cmd::DragStart{event.mouse_col, event.mouse_row};
    }
    return cmd::ClickAt{event.mouse_col, event.mouse_row};
  }
  if (event.id == input::MouseDrag) {
    return cmd::DragUpdate{event.mouse_col, event.mouse_row};
  }
  if (event.id == input::MouseRelease) {
    if (mode == InputMode::Visual || mode == InputMode::VisualBlock) {
      return cmd::DragEnd{event.mouse_col, event.mouse_row};
    }
    return std::nullopt;
  }

  bool is_press = event.type == EventType::Press || event.type == EventType::Unknown;
  if (!is_press) {
    return std::nullopt;
  }

  // Link hints mode input
  if (mode == InputMode::LinkHints) {
    if (event.id == 27) {
      return cmd::LinkHintCancel{};
    }
    if (event.id >= 'a' && event.id <= 'z') {
      return cmd::LinkHintKey{static_cast<char>(event.id)};
    }
    return std::nullopt;
  }

  // Visual mode input
  if (mode == InputMode::Visual || mode == InputMode::VisualBlock) {
    if (event.id == 27) {
      return cmd::SelectionCancel{};
    }

    auto* info = bindings_.lookup(event.id);
    if (info) {
      if (info->name == cmd::EnterVisualMode::Action) {
        return (mode == InputMode::Visual) ? Command{cmd::SelectionCancel{}} : Command{cmd::EnterVisualMode{}};
      }
      if (info->name == cmd::EnterVisualBlockMode::Action) {
        return (mode == InputMode::VisualBlock) ? Command{cmd::SelectionCancel{}} : Command{cmd::EnterVisualBlockMode{}};
      }
      if (info->name == cmd::SelectionYank::Action) {
        return cmd::SelectionYank{};
      }
      if (info->name == cmd::ScrollLeft::Action) {
        return cmd::SelectionMove{-1, 0};
      }
      if (info->name == cmd::ScrollRight::Action) {
        return cmd::SelectionMove{1, 0};
      }
      if (info->name == cmd::ScrollDown::Action) {
        return cmd::SelectionMove{0, 1};
      }
      if (info->name == cmd::ScrollUp::Action) {
        return cmd::SelectionMove{0, -1};
      }
    }

    // Hard-coded visual mode motions (overlap normal-mode bindings)
    if (event.id == 'w') {
      pending_prefix_ = false;
      return cmd::SelectionMoveWord{1};
    }
    if (event.id == 'b') {
      pending_prefix_ = false;
      return cmd::SelectionMoveWord{-1};
    }
    if (event.id == 'e') {
      pending_prefix_ = false;
      return cmd::SelectionGoto{cmd::SelectionTarget::WordEnd};
    }
    if (event.id == '0') {
      pending_prefix_ = false;
      return cmd::SelectionGoto{cmd::SelectionTarget::LineStart};
    }
    if (event.id == '$') {
      pending_prefix_ = false;
      return cmd::SelectionGoto{cmd::SelectionTarget::LineEnd};
    }
    if (event.id == '^') {
      pending_prefix_ = false;
      return cmd::SelectionGoto{cmd::SelectionTarget::FirstNonSpace};
    }
    if (event.id == 'G') {
      pending_prefix_ = false;
      return cmd::SelectionGoto{cmd::SelectionTarget::DocEnd};
    }
    if (event.id == 'g') {
      if (pending_prefix_) {
        pending_prefix_ = false;
        return cmd::SelectionGoto{cmd::SelectionTarget::DocStart};
      }
      pending_prefix_ = true;
      return std::nullopt;
    }

    pending_prefix_ = false;
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
      auto* seq_info = bindings_.sequence_double_info();
      if (seq_info) {
        return seq_info->make();
      }
      return std::nullopt;
    }
    pending_prefix_ = true;
    return std::nullopt;
  }

  // Handle last-page action specially for count-prefix goto-page
  auto* info = bindings_.lookup(event.id);
  if (info && info->name == cmd::GotoLastPage::Action && event.modifiers == 0) {
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
  if (info) {
    return info->make();
  }

  // Resize is not configurable
  if (event.id == input::Resize) {
    return cmd::Resize{};
  }

  return std::nullopt;
}
