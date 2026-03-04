#pragma once

#include "color.h"

#include <cstdint>

/// @brief All themed colors for the application.
///
/// Terminal frontend only: statusline, overlay, sidebar colors.
/// Both frontends: link hints, search highlights, document recoloring.
struct ColorScheme {
  // --- Terminal frontend only (Neovim has its own widgets) ---

  Color statusline_fg = Color::terminal_default(); ///< Statusline foreground.
  Color statusline_bg = Color::terminal_default(); ///< Statusline background.
  bool statusline_reverse = true;                  ///< Use reverse video (cleared when explicit colors set).

  Color overlay_fg = Color::terminal_default(); ///< Overlay foreground.
  Color overlay_bg = Color::terminal_default(); ///< Overlay background.
  bool overlay_reverse = true;                  ///< Use reverse video (cleared when explicit colors set).

  Color sidebar_fg = Color::terminal_default();        ///< Sidebar foreground.
  Color sidebar_bg = Color::terminal_default();        ///< Sidebar background.
  Color sidebar_active_fg = Color::terminal_default(); ///< Sidebar active item foreground.
  Color sidebar_active_bg = Color::terminal_default(); ///< Sidebar active item background.
  Color sidebar_border = Color::terminal_default();    ///< Sidebar border color.

  // --- Both frontends (rendered by C++ server) ---

  Color link_hint_fg = Color::rgb(0, 0, 0);     ///< Link hint label foreground.
  Color link_hint_bg = Color::rgb(255, 255, 0); ///< Link hint label background.

  Color search_highlight = Color::rgb(255, 255, 0); ///< Background match highlight color.
  uint8_t search_highlight_alpha = 80;              ///< Background match blend factor.
  Color search_active = Color::rgb(255, 165, 0);    ///< Active/focused match highlight color.
  uint8_t search_active_alpha = 120;                ///< Active match blend factor.

  Color recolor_dark = Color::terminal_default();  ///< Replaces black/text in terminal theme.
  Color recolor_light = Color::terminal_default(); ///< Replaces white/background in terminal theme.
};
