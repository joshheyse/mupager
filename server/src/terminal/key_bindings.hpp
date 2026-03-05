#pragma once

#include "action.hpp"
#include "input_event.hpp"

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

  /// @brief Parse a key spec string.
  /// Accepts single chars ("j"), special names ("Tab", "Esc", "Up", etc.),
  /// ctrl combos ("Ctrl+F"), and double-key sequences ("gg").
  /// @return The parsed KeySpec, or nullopt on invalid input.
  static std::optional<KeySpec> parse(std::string_view spec);
};

/// @brief Display-only help entry for the help overlay.
struct HelpBinding {
  std::string key_label;   ///< Key combination display string.
  std::string description; ///< What the binding does.
};

/// @brief A registered bindable action: name, description, and factory.
struct ActionEntry {
  std::string_view name;        ///< Action name (e.g. "scroll_down").
  std::string_view description; ///< Human-readable description.
  Action (*make)();             ///< Factory to create a default Action instance.
};

/// @brief Configurable key binding registry.
///
/// Maps key IDs to ActionEntry pointers and tracks per-action key lists for help display.
/// Supports a single double-key sequence prefix (e.g. 'g' for "gg").
class KeyBindings {
public:
  /// @brief Build the default key bindings.
  static KeyBindings defaults();

  /// @brief Bind a key to an action.
  void bind(const ActionEntry* entry, const KeySpec& key);

  /// @brief Remove all bindings for an action.
  void clear(const ActionEntry* entry);

  /// @brief Look up the action bound to a key ID.
  /// @return Pointer to ActionEntry, or nullptr if unbound.
  const ActionEntry* lookup(uint32_t key_id) const;

  /// @brief The key ID that triggers sequence mode (e.g. 'g'), or 0 if none.
  uint32_t sequence_prefix_key() const {
    return prefix_key_;
  }

  /// @brief The action dispatched when the sequence prefix is doubled (e.g. "gg").
  /// @return Pointer to ActionEntry, or nullptr if no sequence is configured.
  const ActionEntry* sequence_double_entry() const;

  /// @brief Build the help bindings list in display order.
  std::vector<HelpBinding> help_bindings() const;

  /// @brief Look up an ActionEntry by name.
  /// @return Pointer to the entry, or nullptr if not found.
  static const ActionEntry* entry_from_name(std::string_view name);

  /// @brief Number of registered action entries.
  static size_t entry_count();

private:
  std::unordered_map<uint32_t, const ActionEntry*> key_to_entry_;
  std::unordered_map<const ActionEntry*, std::vector<KeySpec>> entry_to_keys_;
  uint32_t prefix_key_ = 0;
  const ActionEntry* double_prefix_entry_ = nullptr;
  bool has_sequence_ = false;
};
