# Neovim Mode

## Overview

In Neovim mode, mupager runs as a backend server process launched by the Neovim plugin. The plugin intercepts document buffers, starts the server via `jobstart` with RPC, and the server renders directly to the terminal through Neovim's channel.

```
+------------+       RPC (Unix socket)       +----------------+
|   Neovim   | <---------------------------> |  Doc Server    |
|            |   keybinds/mouse → commands   |   (C++)        |
|  buffer =  |   ← kitty image escapes      |                |
|  "canvas"  |                               |  libmupdf      |
+------------+                               +----------------+
      |                                             |
      v                                             v
  Kitty terminal                          rendering + compositing
  (displays images via graphics protocol)
```

### How It Works

1. **Intercept:** The plugin registers a `BufReadCmd` autocmd for document file patterns (e.g., `*.pdf`, `*.epub`)
2. **Buffer:** A scratch buffer is created as a "canvas" — the actual content is rendered as terminal images
3. **Server:** `jobstart` launches the mupager binary with `--mode neovim` and RPC enabled
4. **RPC:** Neovim sends user input events (keys, mouse, resize) to the server via `vim.rpcnotify`; the server sends Kitty graphics escapes back via `nvim_chan_send(1, ...)`
5. **Display:** The terminal renders the images at the correct position using Kitty graphics protocol

## Installation

### lazy.nvim

```lua
{
  "joshheyse/mupager",
  ft = { "pdf" },
  config = function()
    require("mupager").setup()
  end,
}
```

### packer.nvim

```lua
use {
  "joshheyse/mupager",
  config = function()
    require("mupager").setup()
  end,
}
```

## Setup Options

Call `require("mupager").setup(opts)` to initialize the plugin. Options are merged with values from `config.toml` (setup options take precedence).

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `bin` | string | `"mupager"` | Path to mupager binary |
| `view_mode` | string | `"continuous"` | View mode: `continuous`, `page`, `page-height`, `side-by-side` |
| `render_scale` | string | `"auto"` | Render scale: `auto`, `never`, `0.25`, `0.5`, `1`, `2`, `4` |
| `theme` | string | `"dark"` | Theme: `dark`, `light`, `auto`, `terminal` |
| `scroll_lines` | integer | `3` | Lines per scroll step |
| `log_level` | string | `"info"` | Log level: `trace`, `debug`, `info`, `warn`, `error`, `critical` |
| `show_stats` | boolean | `false` | Show cache stats in statusline |
| `watch` | boolean | `false` | Auto-reload on file changes |
| `converter` | string | nil | Shell command for file conversion |
| `converters` | table | nil | Pattern → command converter map (e.g., `{ ["*.md"] = "pandoc %i -o %o" }`) |
| `patterns` | table | (see below) | File patterns that trigger mupager |
| `poll_interval` | integer | `100` | Tmux focus poll interval in ms |
| `fallback_bg` | integer | nil | Fallback background color (0xRRGGBB) for transparent themes |
| `fallback_fg` | integer | nil | Fallback foreground color (0xRRGGBB) for transparent themes |

### Default File Patterns

The plugin intercepts these patterns by default (binary document formats only):

```lua
{ "*.pdf", "*.epub", "*.xps", "*.oxps", "*.cbz", "*.cbr", "*.fb2", "*.mobi" }
```

Text-editable formats (HTML, SVG, images) require explicit opt-in via the `patterns` option to avoid intercepting files you'd normally edit.

## Commands

| Command | Description |
|---------|-------------|
| `:MupagerOpen {file}` | Open a document in mupager (with file completion) |
| `:MupagerClose` | Close the current mupager session |
| `:MupagerLog` | Open the mupager log file for debugging |

## Keybindings

The plugin sets buffer-local normal-mode keymaps on the mupager buffer.

### Scrolling

| Key | Description |
|-----|-------------|
| `j` / `Down` | Scroll down (accepts count) |
| `k` / `Up` | Scroll up (accepts count) |
| `d` | Half page down |
| `u` | Half page up |
| `Ctrl+F` | Page down |
| `Ctrl+B` | Page up |
| `h` / `Left` | Scroll left (accepts count) |
| `l` / `Right` | Scroll right (accepts count) |
| `ScrollWheelUp` / `ScrollWheelDown` | Mouse scroll |

### Navigation

| Key | Description |
|-----|-------------|
| `gg` | First page (or go to page with count) |
| `G` | Last page (or go to page with count) |
| `H` | Jump back |
| `L` | Jump forward |

### Zoom

| Key | Description |
|-----|-------------|
| `+` / `=` | Zoom in |
| `-` | Zoom out |
| `0` / `w` | Fit width |

### View

| Key | Description |
|-----|-------------|
| `Tab` | Toggle view mode |
| `t` | Toggle theme |

### Search

| Key | Description |
|-----|-------------|
| `/` | Search (opens `vim.ui.input` prompt) |
| `n` | Next match |
| `N` | Previous match |
| `Esc` | Clear search |

### Other

| Key | Description |
|-----|-------------|
| `o` | Table of contents (Telescope picker) |
| `f` | Link hints |
| `R` | Reload document |

> **Note:** Sidebar (`e`), help overlay (`?`), and command mode (`:`) are terminal-mode-only features. In Neovim, the TOC uses a Telescope picker instead of an overlay.

## Statusline

The mupager buffer automatically sets a custom statusline showing:

- **Left:** search term and match count
- **Right:** view mode, zoom level, theme, page number, and cache stats (if enabled)

To use in a custom statusline plugin:

```lua
require("mupager").statusline()
```

## API

### `require("mupager").open(file)`

Open a document file. Creates a scratch buffer, starts the server, sets up keybindings and autocmds.

### `require("mupager").close()`

Close the current session. Stops the server, removes autocmds, restores highlight groups, deletes the buffer.

### `require("mupager").statusline()`

Returns a formatted statusline string from the current server state.

## Float Window Handling

Kitty renders images at a z-index below cells that have non-default backgrounds. When floating windows (Telescope, noice, notifications) open over a mupager buffer, their transparent cells would let the image show through.

mupager handles this automatically:

1. On open, it forces opaque backgrounds on common float highlight groups (`NormalFloat`, `FloatBorder`, `TelescopeNormal`, etc.)
2. A periodic timer (default 100ms) checks all windows and applies opaque backgrounds to any new floating windows
3. The background color is nudged by 1 unit in the blue channel to avoid matching Kitty's exact default background — this works around [kitty #7563](https://github.com/kovidgoyal/kitty/issues/7563) where Kitty treats cells with its exact default bg as transparent

For transparent Neovim themes where no highlight group has an explicit background, set `fallback_bg` in setup:

```lua
require("mupager").setup({
  fallback_bg = 0x1a1b26,  -- your terminal's actual background color
})
```

## Tmux Visibility Management

When running inside tmux, Kitty images persist at terminal-absolute coordinates even when you switch tmux windows. mupager handles this with a focus polling timer:

- Every `poll_interval` ms (default 100), it checks `#{window_active}` via `tmux display-message`
- When the tmux window becomes inactive, the image is hidden
- When it becomes active again, the image is shown

This is more reliable than `FocusLost`/`FocusGained` autocmds, which miss tmux keybinding-driven window switches.

## See Also

- `:help mupager` — Neovim help documentation
- [[Terminal Mode]] — standalone mode differences
- [[Configuration]] — full config reference
