#pragma once

#include "color.h"

#include <cstdint>

/// @brief All themed colors for the application.
///
/// Terminal frontend only: statusline, overlay, sidebar colors.
/// Both frontends: link hints, search highlights, document recoloring.
struct ColorScheme {
  // --- Terminal frontend only (Neovim has its own widgets) ---

  Color statusline_fg = Color::indexed(146); ///< Statusline foreground.
  Color statusline_bg = Color::indexed(236); ///< Statusline background.

  Color overlay_fg = Color::indexed(146);     ///< Overlay foreground.
  Color overlay_bg = Color::indexed(234);     ///< Overlay background.
  Color overlay_border = Color::indexed(111); ///< Overlay border color.

  Color sidebar_fg = Color::terminal_default();  ///< Sidebar foreground.
  Color sidebar_bg = Color::terminal_default();  ///< Sidebar background.
  Color sidebar_active_fg = Color::indexed(153); ///< Sidebar active item foreground.
  Color sidebar_active_bg = Color::indexed(60);  ///< Sidebar active item background.
  Color sidebar_border = Color::indexed(59);     ///< Sidebar border color.

  // --- Both frontends (rendered by C++ server) ---

  Color link_hint_fg = Color::indexed(234); ///< Link hint label foreground.
  Color link_hint_bg = Color::indexed(180); ///< Link hint label background.

  Color search_highlight = Color::rgb(255, 255, 0); ///< Background match highlight color.
  uint8_t search_highlight_alpha = 80;              ///< Background match blend factor.
  Color search_active = Color::rgb(255, 165, 0);    ///< Active/focused match highlight color.
  uint8_t search_active_alpha = 120;                ///< Active match blend factor.

  Color recolor_dark = Color::terminal_default();  ///< Replaces black/text in terminal theme.
  Color recolor_light = Color::terminal_default(); ///< Replaces white/background in terminal theme.
};
