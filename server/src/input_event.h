#pragma once

#include <cstdint>

/// @brief Project-level input constants, decoupled from any terminal library.
namespace input {
static constexpr uint32_t RESIZE = 0xFFFF'FF00;
static constexpr uint32_t BACKSPACE = 0xFFFF'FF01;
static constexpr uint32_t ARROW_UP = 0xFFFF'FF02;
static constexpr uint32_t ARROW_DOWN = 0xFFFF'FF03;
static constexpr uint32_t PAGE_UP = 0xFFFF'FF04;
static constexpr uint32_t PAGE_DOWN = 0xFFFF'FF05;
static constexpr uint32_t HOME = 0xFFFF'FF06;
static constexpr uint32_t END = 0xFFFF'FF07;
static constexpr uint32_t ARROW_LEFT = 0xFFFF'FF08;
static constexpr uint32_t ARROW_RIGHT = 0xFFFF'FF09;
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
