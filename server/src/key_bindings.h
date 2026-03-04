#pragma once

#include "input_event.h"
#include "rpc_command.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// @brief Bindable actions for normal-mode key dispatch.
enum class Action {
  SCROLL_DOWN,
  SCROLL_UP,
  HALF_PAGE_DOWN,
  HALF_PAGE_UP,
  PAGE_DOWN,
  PAGE_UP,
  SCROLL_LEFT,
  SCROLL_RIGHT,
  ZOOM_IN,
  ZOOM_OUT,
  ZOOM_RESET,
  TOGGLE_VIEW,
  TOGGLE_THEME,
  QUIT,
  FIRST_PAGE,
  LAST_PAGE,
  JUMP_BACK,
  JUMP_FORWARD,
  LINK_HINTS,
  COMMAND_MODE,
  SEARCH,
  NEXT_MATCH,
  PREV_MATCH,
  OUTLINE,
  SIDEBAR,
  HELP,
  CLEAR_SEARCH,
  VISUAL_MODE,
  VISUAL_BLOCK_MODE,
  VISUAL_YANK,
  VISUAL_NEXT_WORD,
  VISUAL_PREV_WORD,
  VISUAL_WORD_END,
  VISUAL_LINE_START,
  VISUAL_LINE_END,
  VISUAL_FIRST_NON_SPACE,
  VISUAL_DOC_START,
  VISUAL_DOC_END,
};

/// @brief A single key specification for binding.
struct KeySpec {
  uint32_t id;       ///< Key ID (char code or input:: constant).
  std::string label; ///< Display label for help overlay.
  bool is_sequence;  ///< True for "gg"-style double-key sequences.
};

/// @brief Display-only help entry for the help overlay.
struct HelpBinding {
  std::string key_label;   ///< Key combination display string.
  std::string description; ///< What the binding does.
};

/// @brief Static table entry mapping action name/description/enum.
struct ActionInfo {
  const char* name;        ///< Config key name (e.g. "scroll-down").
  const char* description; ///< Human-readable description (e.g. "Scroll Down").
  Action action;           ///< Enum value.
};

/// @brief The action table in help display order — single source of truth.
extern const ActionInfo ACTION_TABLE[];

/// @brief Number of entries in ACTION_TABLE.
extern const size_t ACTION_TABLE_SIZE;

/// @brief Parse a key spec string into a KeySpec.
/// Accepts single chars ("j"), special names ("Tab", "Esc", "Up", etc.),
/// ctrl combos ("Ctrl+F"), and double-key sequences ("gg").
/// @return The parsed KeySpec, or nullopt on invalid input.
std::optional<KeySpec> parse_key_spec(const std::string& spec);

/// @brief Look up an Action by its config name.
std::optional<Action> action_from_name(std::string_view name);

/// @brief Get the description string for an action.
const char* action_description(Action a);

/// @brief Convert an Action to its corresponding RpcCommand.
RpcCommand action_to_command(Action a);

/// @brief Configurable key binding registry.
///
/// Maps key IDs to actions and tracks per-action key lists for help display.
/// Supports a single double-key sequence prefix (e.g. 'g' for "gg").
class KeyBindings {
public:
  /// @brief Build the default key bindings.
  static KeyBindings defaults();

  /// @brief Bind a key to an action.
  void bind(Action action, const KeySpec& key);

  /// @brief Remove all bindings for an action.
  void clear(Action action);

  /// @brief Look up the action bound to a key ID.
  std::optional<Action> lookup(uint32_t key_id) const;

  /// @brief The key ID that triggers sequence mode (e.g. 'g'), or 0 if none.
  uint32_t sequence_prefix_key() const {
    return prefix_key_;
  }

  /// @brief The action dispatched when the sequence prefix is doubled (e.g. "gg" → FIRST_PAGE).
  std::optional<Action> sequence_double_action() const;

  /// @brief Build the help bindings list in display order.
  std::vector<HelpBinding> help_bindings() const;

private:
  std::unordered_map<uint32_t, Action> key_to_action_;
  std::unordered_map<int, std::vector<KeySpec>> action_to_keys_; ///< Keyed by static_cast<int>(Action).
  uint32_t prefix_key_ = 0;
  Action double_prefix_action_ = Action::FIRST_PAGE;
  bool has_sequence_ = false;
};
