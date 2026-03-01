#pragma once

#include <cstdint>

/// @brief Project-level input constants, decoupled from any terminal library.
namespace input {
static constexpr uint32_t RESIZE = 0xFFFF'FF00;
} // namespace input

/// @brief Input event types.
enum class EventType {
  UNKNOWN,
  PRESS,
  REPEAT,
  RELEASE
};

/// @brief Thin input event wrapper, independent of the terminal backend.
struct InputEvent {
  uint32_t id;        ///< Unicode codepoint or project-level constant (e.g. input::RESIZE).
  unsigned modifiers; ///< Modifier bitmask.
  EventType type;     ///< Key event type.
};
