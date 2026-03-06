#pragma once

#include "color.hpp"

#include <optional>
#include <utility>

/// @brief Query the terminal for foreground and background colors via OSC 10/11.
///
/// Must be called BEFORE ncurses initscr() as it temporarily sets /dev/tty to raw mode.
/// Wraps queries in DCS passthrough when $TMUX is set.
///
/// @param timeout_ms Maximum time to wait for a response.
/// @return Pair of (foreground, background) colors, or nullopt if the terminal doesn't respond.
std::optional<std::pair<Color, Color>> query_terminal_colors(int timeout_ms = 200);
