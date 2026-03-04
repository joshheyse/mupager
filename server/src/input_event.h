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
static constexpr uint32_t RPC_COMMAND = 0xFFFF'FF0A;     ///< Sentinel: an RPC command is queued.
static constexpr uint32_t MOUSE_SCROLL_UP = 0xFFFF'FF10; ///< Mouse scroll wheel up.
static constexpr uint32_t MOUSE_SCROLL_DN = 0xFFFF'FF11; ///< Mouse scroll wheel down.
static constexpr uint32_t MOUSE_PRESS = 0xFFFF'FF12;     ///< Mouse button press.
static constexpr uint32_t MOUSE_RELEASE = 0xFFFF'FF13;   ///< Mouse button release.
static constexpr uint32_t MOUSE_DRAG = 0xFFFF'FF14;      ///< Mouse drag (button held + motion).

static constexpr unsigned MOD_CTRL = 1;  ///< Ctrl modifier bitmask.
static constexpr unsigned MOD_SHIFT = 2; ///< Shift modifier bitmask.
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
  int mouse_col = 0;  ///< Cell column (valid for MOUSE_* events).
  int mouse_row = 0;  ///< Cell row (valid for MOUSE_* events).
};
