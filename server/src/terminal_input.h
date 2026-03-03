#pragma once

#include "input_event.h"
#include "rpc_command.h"

#include <optional>
#include <vector>

enum class InputMode;

/// @brief Display-only help entry for the help overlay.
struct HelpBinding {
  const char* key_label;   ///< Key combination display string.
  const char* description; ///< What the binding does.
};

/// @brief Get the help bindings table for the help overlay.
const std::vector<HelpBinding>& get_help_bindings();

/// @brief Translates terminal key events into RpcCommands.
///
/// Owns vim-style prefix state (pending_g_, pending_count_) and converts
/// raw InputEvents into commands based on the current input mode.
class TerminalInputHandler {
public:
  /// @brief Translate a key event into a command.
  /// @param event The input event to translate.
  /// @param mode Current input mode of the application.
  /// @param terminal_rows Number of terminal rows (for page-size calculations).
  /// @return The translated command, or nullopt if the event was consumed without producing a command.
  std::optional<RpcCommand> translate(const InputEvent& event, InputMode mode, int terminal_rows);

private:
  bool pending_g_ = false;
  int pending_count_ = 0;
};
