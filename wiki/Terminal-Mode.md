# Terminal Mode

## Overview

Terminal mode is mupager's standalone TUI frontend. It reads keyboard and mouse input directly from the terminal and writes Kitty graphics protocol escapes to stdout.

```
+-------------------+
|    Doc Server     |
|      (C++)        |
|                   |
|  stdin  → input   |
|  stdout → kitty   |
|  libmupdf         |
+-------------------+
         |
         v
   Kitty terminal
```

## Starting

```bash
mupager document.pdf
```

See [[Command Line Reference]] for all options.

## Keybindings

All keybindings are customizable via the `[keys]` config section — see [[Configuration]].

### Scrolling

| Key | Description |
|-----|-------------|
| `j` / `Down` | Scroll down (accepts count prefix) |
| `k` / `Up` | Scroll up (accepts count prefix) |
| `d` | Half page down |
| `u` | Half page up |
| `Ctrl+F` | Page down |
| `Ctrl+B` | Page up |
| `h` / `Left` | Scroll left (accepts count prefix) |
| `l` / `Right` | Scroll right (accepts count prefix) |

### Navigation

| Key | Description |
|-----|-------------|
| `gg` | First page |
| `G` | Last page |
| `[n]gg` / `[n]G` | Go to page n |
| `H` | Jump back in history |
| `L` | Jump forward in history |

### Zoom

| Key | Description |
|-----|-------------|
| `+` / `=` | Zoom in |
| `-` | Zoom out |
| `0` / `w` | Fit width (zoom reset) |

### View and Theme

| Key | Description |
|-----|-------------|
| `Tab` | Toggle view mode (continuous → page → page-height → side-by-side) |
| `t` | Toggle theme (dark → light → auto → terminal) |

### Search

| Key | Description |
|-----|-------------|
| `/` | Enter search mode — type query, press Enter |
| `n` | Next match |
| `N` | Previous match |
| `Esc` | Clear search |

### Visual Selection

| Key | Description |
|-----|-------------|
| `v` | Enter visual (character) selection mode |
| `Ctrl+V` | Enter visual block selection mode |
| `h`/`j`/`k`/`l` | Move selection cursor |
| `w` / `b` | Move by word forward / backward |
| `e` | Move to word end |
| `0` / `$` / `^` | Line start / line end / first non-space |
| `gg` / `G` | Document start / end |
| `y` | Yank selected text (copies via OSC 52) |
| `Esc` | Cancel selection |

### Other

| Key | Description |
|-----|-------------|
| `o` | Open table of contents overlay |
| `e` | Toggle sidebar TOC |
| `f` | Activate link hints |
| `?` | Toggle help overlay |
| `:` | Enter command mode |
| `q` | Quit |

## Count Prefixes

Scroll commands accept a count prefix: type a number before the key to repeat. For example, `5j` scrolls down 5 steps. `[n]gg` and `[n]G` jump to page n.

## Command Mode

Press `:` to enter command mode. Available commands:

| Command | Description |
|---------|-------------|
| `:reload` | Reload the document from disk |
| `:[n]` | Go to page n |
| `:set view-mode MODE` | Change view mode |
| `:set theme THEME` | Change theme |
| `:set render-scale SCALE` | Change render scale |

## UI Elements

### Statusline

The bottom row shows:

- **Left:** search term and match count (e.g., `/search [3/12]`)
- **Right:** view mode, zoom level (if not 100%), theme, page number (e.g., `continuous | 150% | DARK | 3/42`)

### Help Overlay (`?`)

Centered popup showing all keybindings. Press `?` again or `Esc` to dismiss.

### TOC Overlay (`o`)

Centered popup showing the document's table of contents. Navigate with `j`/`k`, jump to a heading with `Enter`, dismiss with `Esc`.

### Sidebar (`e`)

Persistent left-side panel showing the TOC. The active heading is highlighted. Toggle with `e`.

### Link Hints (`f`)

Labels appear on all links on visible pages. Type the label characters to follow a link. Internal links jump within the document; external links open in the browser.

### Visual Selection (`v` / `Ctrl+V`)

A highlighted region overlaid on the page. Move with vim motions, yank with `y`.

## Differences from Neovim Mode

| Feature | Terminal Mode | Neovim Mode |
|---------|--------------|-------------|
| Sidebar (`e`) | Built-in | Not available |
| Help overlay (`?`) | Built-in | Not available |
| Command mode (`:`) | Built-in | Not available |
| TOC | Overlay (`o`) | Telescope picker (`o`) |
| Search input | Built-in prompt | `vim.ui.input` |
| Reload | `:reload` command | `R` key |
| Quit | `q` quits process | `q` closes buffer |
| Statusline | Built-in renderer | Neovim statusline |
