# mupager

Terminal document viewer with pixel-perfect rendering via the Kitty graphics protocol.

<!-- screenshot placeholder -->

## Features

- PDF, EPUB, XPS, CBZ/CBR, FB2, MOBI, SVG, HTML, and image rendering (via MuPDF)
- Pixel-perfect page rendering with the Kitty graphics protocol
- Continuous scroll, single-page, page-height, and side-by-side view modes
- Vim-style search with highlighted matches
- Table of contents overlay and sidebar
- Link hints for clickable URLs and internal references
- Configurable color themes (dark, light, auto, terminal)
- Fully customizable keybindings
- Tmux support (with `allow-passthrough`)
- Neovim integration as a plugin backend

## Install

### Nix

```bash
# Run directly
nix run github:joshheyse/mupager -- document.pdf

# Install to profile
nix profile install github:joshheyse/mupager
```

### Pre-built binaries

Download from the [Releases](https://github.com/joshheyse/mupager/releases) page.

### From source

```bash
git clone https://github.com/joshheyse/mupager.git
cd mupager
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build
# Binary at build/src/mupager
```

## Quickstart

### Terminal

```bash
mupager document.pdf
```

Press `?` to open the help overlay with a full list of keybindings.

### Neovim

Add the plugin with [lazy.nvim](https://github.com/folke/lazy.nvim):

```lua
{
  "joshheyse/mupager.nvim",
  ft = { "pdf" },
  config = function()
    require("mupager").setup()
  end,
}
```

## Configuration

Configuration file location: `~/.config/mupager/config.toml` (or `$XDG_CONFIG_HOME/mupager/config.toml`).

### Top-level keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `view-mode` | string | `"continuous"` | View mode (`continuous`, `page`, `page-height`, `side-by-side`) |
| `theme` | string | `"dark"` | Color theme (`dark`, `light`, `auto`, `terminal`) |
| `render-scale` | string | `"auto"` | Render scale (`auto`, `never`, `0.25`, `0.5`, `1`, `2`, `4`) |
| `scroll-lines` | integer | `3` | Lines per scroll step |
| `max-page-cache` | integer | `64` | Max page cache size in MB |
| `log-level` | string | `"info"` | Log level (`trace`, `debug`, `info`, `warn`, `error`, `critical`) |
| `log-file` | string | (auto) | Log file path |
| `show-stats` | boolean | `false` | Show cache stats in statusline |
| `terminal-fg` | string | (detect) | Override terminal foreground color (`#RRGGBB`) |
| `terminal-bg` | string | (detect) | Override terminal background color (`#RRGGBB`) |

### `[colors]` section

| Key | Description |
|-----|-------------|
| `statusline-fg` / `statusline-bg` | Statusline colors |
| `overlay-fg` / `overlay-bg` / `overlay-border` | TOC/help overlay |
| `sidebar-fg` / `sidebar-bg` / `sidebar-active-fg` / `sidebar-active-bg` / `sidebar-border` | Sidebar |
| `link-hint-fg` / `link-hint-bg` | Link hint labels |
| `search-highlight` / `search-highlight-alpha` | Search match highlight |
| `search-active` / `search-active-alpha` | Active search match |
| `recolor-dark` / `recolor-light` / `recolor-accent` | Terminal theme recoloring |

Color values are specified as `"#RRGGBB"`, `"default"`, or a 256-color index (integer 0-255). Alpha values are integers 0-255.

### `[keys]` section

Keybindings are configured by mapping action names to key specs. Key specs can be single characters (`"j"`), special names (`"Tab"`, `"Esc"`, `"Up"`, etc.), ctrl combos (`"Ctrl+F"`), or double-key sequences (`"gg"`).

See the keybindings table below for all available actions and their defaults.

## Keybindings

| Action | Default Keys | Description |
|--------|-------------|-------------|
| `scroll-down` | `j`, `Down` | Scroll down |
| `scroll-up` | `k`, `Up` | Scroll up |
| `half-page-down` | `d` | Half page down |
| `half-page-up` | `u` | Half page up |
| `page-down` | `Ctrl+F` | Page down |
| `page-up` | `Ctrl+B` | Page up |
| `scroll-left` | `h`, `Left` | Scroll left |
| `scroll-right` | `l`, `Right` | Scroll right |
| `zoom-in` | `+`, `=` | Zoom in |
| `zoom-out` | `-` | Zoom out |
| `zoom-reset` | `0`, `w` | Fit width |
| `toggle-view` | `Tab` | Toggle view mode |
| `toggle-theme` | `t` | Toggle theme |
| `quit` | `q` | Quit |
| `first-page` | `gg` | First page |
| `last-page` | `G` | Last page |
| `jump-back` | `H` | Jump back |
| `jump-forward` | `L` | Jump forward |
| `link-hints` | `f` | Link hints |
| `command-mode` | `:` | Command mode |
| `search` | `/` | Search |
| `next-match` | `n` | Next match |
| `prev-match` | `N` | Previous match |
| `outline` | `o` | Table of contents |
| `sidebar` | `e` | Toggle sidebar TOC |
| `help` | `?` | Toggle help |
| `clear-search` | `Esc` | Clear search |

Additional commands: `[n]gg` / `[n]G` go to page n, `:reload` reloads the document.

## Terminal Compatibility

- **Kitty** -- Full support (native graphics protocol)
- **WezTerm** -- Full support (Kitty graphics protocol compatible)
- **Ghostty** -- Full support (Kitty graphics protocol compatible)
- **Tmux** -- Requires `set -g allow-passthrough on` in `tmux.conf`

## Diagnostics

Run `mupager --diagnose` to print environment information for troubleshooting.

## License

MIT. See [LICENSE](LICENSE).
