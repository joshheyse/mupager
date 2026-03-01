#pragma once

#include <cstdint>

/// @brief Input event types.
enum class EventType { UNKNOWN, PRESS, REPEAT, RELEASE };

/// @brief Thin wrapper around notcurses input, keeping ncinput out of App/KeyMap layers.
struct InputEvent {
  uint32_t id;         ///< Unicode codepoint or NCKEY_* constant.
  unsigned modifiers;  ///< NCKEY_MOD_* bitmask.
  EventType type;      ///< Key event type.
};
