#pragma once

/// @brief Commands dispatched from input events.
enum class Command {
  QUIT,
  RESIZE,
  SCROLL_DOWN,
  SCROLL_UP,
  HALF_PAGE_DOWN,
  HALF_PAGE_UP,
  GOTO_FIRST_PAGE,
  GOTO_LAST_PAGE,
  PAGE_DOWN,
  PAGE_UP,
  TOGGLE_VIEW_MODE,
};
