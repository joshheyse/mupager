# Configuration

## File Location

mupager reads configuration from:

```
$XDG_CONFIG_HOME/mupager/config.toml
```

This is typically `~/.config/mupager/config.toml`. Override with `--config PATH` or `-c PATH`.

## Precedence

Settings are resolved in this order (highest priority first):

1. **CLI flags**
2. **Neovim `setup()` options** (passed as CLI flags to the server)
3. **config.toml**
4. **Built-in defaults**

## Top-Level Keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `view-mode` | string | `"continuous"` | View mode: `continuous`, `page`, `page-height`, `side-by-side` |
| `theme` | string | `"dark"` | Color theme: `dark`, `light`, `auto`, `terminal` |
| `render-scale` | string | `"auto"` | Render scale: `auto`, `never`, `0.25`, `0.5`, `1`, `2`, `4` |
| `scroll-lines` | integer | `3` | Lines per scroll step |
| `max-page-cache` | integer | `64` | Max page cache size in MB |
| `log-level` | string | `"info"` | Log level: `trace`, `debug`, `info`, `warn`, `error`, `critical` |
| `log-file` | string | (auto) | Log file path (default: `$XDG_STATE_HOME/mupager/mupager.log`) |
| `show-stats` | boolean | `false` | Show cache statistics in statusline |
| `terminal-fg` | string | (detect) | Override terminal foreground color (`#RRGGBB`) |
| `terminal-bg` | string | (detect) | Override terminal background color (`#RRGGBB`) |
| `watch` | boolean | `false` | Auto-reload document when the file changes on disk |

## `[colors]` Section

All color values are specified as `"#RRGGBB"` strings, `"default"`, or a 256-color index (integer 0–255). Alpha values are integers 0–255.

### Statusline

| Key | Description |
|-----|-------------|
| `statusline-fg` | Statusline foreground |
| `statusline-bg` | Statusline background |

### Overlay (TOC, Help)

| Key | Description |
|-----|-------------|
| `overlay-fg` | Overlay foreground |
| `overlay-bg` | Overlay background |
| `overlay-border` | Overlay border color |

### Sidebar

| Key | Description |
|-----|-------------|
| `sidebar-fg` | Sidebar foreground |
| `sidebar-bg` | Sidebar background |
| `sidebar-active-fg` | Sidebar active item foreground |
| `sidebar-active-bg` | Sidebar active item background |
| `sidebar-border` | Sidebar border color |

### Link Hints

| Key | Description |
|-----|-------------|
| `link-hint-fg` | Link hint label foreground |
| `link-hint-bg` | Link hint label background |

### Search and Selection

| Key | Description |
|-----|-------------|
| `search-highlight` | Search match highlight color |
| `search-highlight-alpha` | Search match highlight alpha (0–255) |
| `search-active` | Active search match color |
| `search-active-alpha` | Active search match alpha (0–255) |
| `selection-highlight` | Selection highlight color |
| `selection-highlight-alpha` | Selection highlight alpha (0–255) |

### Theme Recoloring

These colors are used by the `terminal` theme to remap document colors to match your color scheme:

| Key | Description |
|-----|-------------|
| `recolor-dark` | Maps to document foreground (text) |
| `recolor-light` | Maps to document background |
| `recolor-accent` | Tint color for links and accents |

## `[keys]` Section

Keybindings are configured by mapping action names to key specs. When an action is specified, it **replaces all default keys** for that action. Actions not listed keep their defaults.

### Key Spec Format

| Format | Example | Description |
|--------|---------|-------------|
| Single character | `"j"` | A single key press |
| Special name | `"Tab"`, `"Esc"`, `"Up"`, `"Down"`, `"Left"`, `"Right"`, `"PageUp"`, `"PageDown"`, `"Home"`, `"End"`, `"Space"`, `"Enter"`, `"Backspace"` | Named special keys |
| Ctrl combo | `"Ctrl+F"` | Ctrl + key |
| Double-key sequence | `"gg"` | Two-key sequence |
| Array | `["j", "Down"]` | Multiple keys for one action |

### Available Actions

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
| `jump-back` | `H` | Jump back in history |
| `jump-forward` | `L` | Jump forward in history |
| `link-hints` | `f` | Link hints |
| `command-mode` | `:` | Command mode |
| `search` | `/` | Search |
| `next-match` | `n` | Next match |
| `prev-match` | `N` | Previous match |
| `outline` | `o` | Table of contents |
| `sidebar` | `e` | Toggle sidebar TOC |
| `help` | `?` | Toggle help overlay |
| `clear-search` | `Esc` | Clear search |
| `visual-mode` | `v` | Visual select mode |
| `visual-block-mode` | `Ctrl+V` | Visual block select mode |
| `visual-yank` | `y` | Yank selection |

## `[converters]` Section

Maps file glob patterns to shell commands that convert the matched file to PDF. See [[Converters and Watch Mode]] for full details.

```toml
[converters]
"*.md" = "pandoc %i -o %o"
"*.markdown" = "pandoc %i -o %o"
```

Placeholders: `%i` = input path, `%o` = output path (temp PDF), `%d` = temp directory.

## Full Example (Tokyo Night)

```toml
theme = "terminal"
view-mode = "continuous"
render-scale = "0.5"
scroll-lines = 3
max-page-cache = 64
log-level = "debug"
show-stats = true
# terminal-fg = "#c0caf5"
# terminal-bg = "#1a1b26"

[colors]
statusline-fg = "#a9b1d6"
statusline-bg = "#292e42"

overlay-fg = "#a9b1d6"
overlay-bg = "#1a1b26"
overlay-border = "#7aa2f7"

sidebar-fg = "#a9b1d6"
sidebar-bg = "#1a1b26"
sidebar-active-fg = "#c0caf5"
sidebar-active-bg = "#33467c"
sidebar-border = "#3b4261"

link-hint-fg = "#1a1b26"
link-hint-bg = "#e0af68"

search-highlight = "#e0af68"
search-highlight-alpha = 80
selection-highlight = "#7aa2f7"
selection-highlight-alpha = 80

recolor-dark = "#c0caf5"
recolor-light = "#1a1b26"
recolor-accent = "#7aa2f7"

[converters]
"*.md" = "pandoc %i -o %o"
"*.markdown" = "pandoc %i -o %o"

# [keys]
# scroll-down = ["j", "Down"]
# quit = "q"
# first-page = "gg"
```

## Neovim Plugin Config

The Neovim plugin reads top-level keys from config.toml (converting kebab-case to snake_case). Options passed to `require("mupager").setup()` take precedence. See [[Neovim Mode]] for the full setup options table.
