#pragma once

#include "command.h"
#include "input_event.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

/// @brief Static table entry: single source of truth for a bindable action.
///
/// Each entry maps a config name to a human description and a factory that
/// produces the default Command for this action. The ActionTable array
/// defines the canonical set of bindable actions in help-display order.
struct ActionInfo {
  const char* name;        ///< Config key name (from cmd struct's constexpr action).
  const char* description; ///< Human-readable description.
  Command (*make)();       ///< Factory to create a default Command instance.
};

/// @brief The action table in help display order — single source of truth.
extern const ActionInfo ActionTable[];

/// @brief Number of entries in ActionTable.
extern const size_t ActionTableSize;

/// @brief Parse a key spec string into a KeySpec.
/// Accepts single chars ("j"), special names ("Tab", "Esc", "Up", etc.),
/// ctrl combos ("Ctrl+F"), and double-key sequences ("gg").
/// @return The parsed KeySpec, or nullopt on invalid input.
std::optional<KeySpec> parse_key_spec(const std::string& spec);

/// @brief Look up an ActionInfo by its config name.
/// @return Pointer into ActionTable, or nullptr if not found.
const ActionInfo* action_from_name(std::string_view name);

/// @brief Configurable key binding registry.
///
/// Maps key IDs to ActionInfo entries and tracks per-action key lists for help display.
/// Supports a single double-key sequence prefix (e.g. 'g' for "gg").
class KeyBindings {
public:
  /// @brief Build the default key bindings.
  static KeyBindings defaults();

  /// @brief Bind a key to an action.
  void bind(const ActionInfo* info, const KeySpec& key);

  /// @brief Remove all bindings for an action.
  void clear(const ActionInfo* info);

  /// @brief Look up the action bound to a key ID.
  /// @return Pointer into ActionTable, or nullptr if unbound.
  const ActionInfo* lookup(uint32_t key_id) const;

  /// @brief The key ID that triggers sequence mode (e.g. 'g'), or 0 if none.
  uint32_t sequence_prefix_key() const {
    return prefix_key_;
  }

  /// @brief The action dispatched when the sequence prefix is doubled (e.g. "gg").
  /// @return Pointer into ActionTable, or nullptr if no sequence is configured.
  const ActionInfo* sequence_double_info() const;

  /// @brief Build the help bindings list in display order.
  std::vector<HelpBinding> help_bindings() const;

private:
  std::unordered_map<uint32_t, const ActionInfo*> key_to_info_;
  std::unordered_map<const ActionInfo*, std::vector<KeySpec>> info_to_keys_;
  uint32_t prefix_key_ = 0;
  const ActionInfo* double_prefix_info_ = nullptr;
  bool has_sequence_ = false;
};
