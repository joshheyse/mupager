#pragma once

/// @brief Commands dispatched from input events.
enum class Command {
  QUIT,
  RESIZE,
  SCROLL_DOWN,
  SCROLL_UP,
  HALF_PAGE_DOWN,
  HALF_PAGE_UP,
};
