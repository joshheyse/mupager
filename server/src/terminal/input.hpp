#pragma once

#include "command.hpp"
#include "geometry.hpp"
#include "input_event.hpp"
#include "key_bindings.hpp"

#include <optional>

enum class InputMode;

/// @brief Translates terminal key events into Commands.
///
/// Owns vim-style prefix state (pending_prefix_, pending_count_) and converts
/// raw InputEvents into commands based on the current input mode.
class TerminalInputHandler {
public:
  /// @brief Construct with key bindings and scroll step.
  explicit TerminalInputHandler(const KeyBindings& bindings, int scroll_lines = 3)
      : bindings_(bindings)
      , scroll_lines_(scroll_lines) {}

  /// @brief Translate a key event into a command.
  /// @param event The input event to translate.
  /// @param mode Current input mode of the application.
  /// @param terminal_rows Number of terminal rows (for page-size calculations).
  /// @param cell Cell dimensions in pixels (for mouse pixel conversion).
  /// @return The translated command, or nullopt if the event was consumed without producing a command.
  std::optional<Command> translate(const InputEvent& event, InputMode mode, int terminal_rows, CellSize cell = {});

  /// @brief Access the key bindings.
  const KeyBindings& bindings() const {
    return bindings_;
  }

private:
  KeyBindings bindings_;
  int scroll_lines_ = 3;
  bool pending_prefix_ = false;
  int pending_count_ = 0;
};
