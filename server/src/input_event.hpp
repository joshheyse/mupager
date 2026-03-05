#pragma once

#include <cstdint>

/// @brief Project-level input constants, decoupled from any terminal library.
namespace input {
static constexpr uint32_t Resize = 0xFFFF'FF00;
static constexpr uint32_t Backspace = 0xFFFF'FF01;
static constexpr uint32_t ArrowUp = 0xFFFF'FF02;
static constexpr uint32_t ArrowDown = 0xFFFF'FF03;
static constexpr uint32_t PageUp = 0xFFFF'FF04;
static constexpr uint32_t PageDown = 0xFFFF'FF05;
static constexpr uint32_t Home = 0xFFFF'FF06;
static constexpr uint32_t End = 0xFFFF'FF07;
static constexpr uint32_t ArrowLeft = 0xFFFF'FF08;
static constexpr uint32_t ArrowRight = 0xFFFF'FF09;
static constexpr uint32_t RpcCommand = 0xFFFF'FF0A;    ///< Sentinel: an RPC command is queued.
static constexpr uint32_t MouseScrollUp = 0xFFFF'FF10; ///< Mouse scroll wheel up.
static constexpr uint32_t MouseScrollDn = 0xFFFF'FF11; ///< Mouse scroll wheel down.
static constexpr uint32_t MousePress = 0xFFFF'FF12;    ///< Mouse button press.
static constexpr uint32_t MouseRelease = 0xFFFF'FF13;  ///< Mouse button release.
static constexpr uint32_t MouseDrag = 0xFFFF'FF14;     ///< Mouse drag (button held + motion).

static constexpr unsigned ModCtrl = 1;  ///< Ctrl modifier bitmask.
static constexpr unsigned ModShift = 2; ///< Shift modifier bitmask.
} // namespace input

/// @brief Input event types.
enum class EventType {
  Unknown,
  Press,
  Repeat,
  Release
};

/// @brief Thin input event wrapper, independent of the terminal backend.
struct InputEvent {
  uint32_t id;        ///< Unicode codepoint or project-level constant (e.g. input::Resize).
  unsigned modifiers; ///< Modifier bitmask.
  EventType type;     ///< Key event type.
  int mouse_col = 0;  ///< Cell column (valid for MOUSE_* events).
  int mouse_row = 0;  ///< Cell row (valid for MOUSE_* events).
};
