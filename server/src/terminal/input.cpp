#include "terminal/input.hpp"

#include "app.hpp"
#include "action.hpp"
#include "input_event.hpp"
#include "geometry.hpp"

#include <optional>
#include <cstdint>

std::optional<Action> TerminalInputHandler::translate(const InputEvent& event, InputMode mode, int /*terminal_rows*/, CellSize cell) {
  // Handle mouse events before mode dispatch
  if (event.id == input::MouseScrollUp || event.id == input::MouseScrollDn) {
    int sign = (event.id == input::MouseScrollUp) ? -1 : 1;
    if (event.modifiers & input::ModCtrl) {
      return (sign < 0) ? Action{action::ZoomIn{}} : Action{action::ZoomOut{}};
    }
    if (event.modifiers & input::ModShift) {
      return action::MouseScroll{sign * cell.width * scroll_lines_, 0};
    }
    return action::MouseScroll{0, sign * cell.height * scroll_lines_};
  }
  if (event.id == input::MousePress) {
    if (mode == InputMode::Visual || mode == InputMode::VisualBlock) {
      return action::DragStart{event.mouse_col, event.mouse_row};
    }
    return action::ClickAt{event.mouse_col, event.mouse_row};
  }
  if (event.id == input::MouseDrag) {
    return action::DragUpdate{event.mouse_col, event.mouse_row};
  }
  if (event.id == input::MouseRelease) {
    if (mode == InputMode::Visual || mode == InputMode::VisualBlock) {
      return action::DragEnd{event.mouse_col, event.mouse_row};
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
      return action::LinkHintCancel{};
    }
    if (event.id >= 'a' && event.id <= 'z') {
      return action::LinkHintKey{static_cast<char>(event.id)};
    }
    return std::nullopt;
  }

  // Visual mode input
  if (mode == InputMode::Visual || mode == InputMode::VisualBlock) {
    if (event.id == 27) {
      return action::SelectionCancel{};
    }

    auto* info = bindings_.lookup(event.id);
    if (info) {
      if (info->name == action::EnterVisualMode::Name) {
        return (mode == InputMode::Visual) ? Action{action::SelectionCancel{}} : Action{action::EnterVisualMode{}};
      }
      if (info->name == action::EnterVisualBlockMode::Name) {
        return (mode == InputMode::VisualBlock) ? Action{action::SelectionCancel{}} : Action{action::EnterVisualBlockMode{}};
      }
      if (info->name == action::SelectionYank::Name) {
        return action::SelectionYank{};
      }
      if (info->name == action::ScrollLeft::Name) {
        return action::SelectionMove{-1, 0};
      }
      if (info->name == action::ScrollRight::Name) {
        return action::SelectionMove{1, 0};
      }
      if (info->name == action::ScrollDown::Name) {
        return action::SelectionMove{0, 1};
      }
      if (info->name == action::ScrollUp::Name) {
        return action::SelectionMove{0, -1};
      }
    }

    // Hard-coded visual mode motions (overlap normal-mode bindings)
    if (event.id == 'w') {
      pending_prefix_ = false;
      return action::SelectionMoveWord{1};
    }
    if (event.id == 'b') {
      pending_prefix_ = false;
      return action::SelectionMoveWord{-1};
    }
    if (event.id == 'e') {
      pending_prefix_ = false;
      return action::SelectionGoto{action::SelectionTarget::WordEnd};
    }
    if (event.id == '0') {
      pending_prefix_ = false;
      return action::SelectionGoto{action::SelectionTarget::LineStart};
    }
    if (event.id == '$') {
      pending_prefix_ = false;
      return action::SelectionGoto{action::SelectionTarget::LineEnd};
    }
    if (event.id == '^') {
      pending_prefix_ = false;
      return action::SelectionGoto{action::SelectionTarget::FirstNonSpace};
    }
    if (event.id == 'G') {
      pending_prefix_ = false;
      return action::SelectionGoto{action::SelectionTarget::DocEnd};
    }
    if (event.id == 'g') {
      if (pending_prefix_) {
        pending_prefix_ = false;
        return action::SelectionGoto{action::SelectionTarget::DocStart};
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
        return action::GotoPage{page};
      }
      auto* seq_info = bindings_.sequence_double_entry();
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
  if (info && info->name == action::GotoLastPage::Name && event.modifiers == 0) {
    if (pending_count_ > 0) {
      int page = pending_count_;
      pending_count_ = 0;
      pending_prefix_ = false;
      return action::GotoPage{page};
    }
    pending_prefix_ = false;
    pending_count_ = 0;
    return action::GotoLastPage{};
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
    return action::Resize{};
  }

  return std::nullopt;
}
