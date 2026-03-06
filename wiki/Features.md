# Features

## Supported Formats

mupager renders all formats supported by MuPDF through a single pipeline — no format-specific code is needed:

- **PDF**
- **EPUB**
- **XPS / OpenXPS**
- **CBZ / CBR** (comic book archives)
- **FB2**, **MOBI** (ebooks)
- **SVG**
- **HTML / XHTML**
- **Images** (PNG, JPG, BMP, etc.)

Additional formats can be supported via [[Converters and Watch Mode|converters]] (e.g., Markdown → PDF via pandoc).

## View Modes

Toggle between modes with `Tab` or set via `--view-mode` / config.

| Mode | Description |
|------|-------------|
| **continuous** | Smooth vertical scroll through all pages (default) |
| **page** | One page at a time, centered |
| **page-height** | One page at a time, scaled to fill the terminal height |
| **side-by-side** | Two pages displayed side by side |

## Navigation

- **Scroll:** `j`/`k` (or arrow keys) with configurable scroll-lines, `d`/`u` for half-page, `Ctrl+F`/`Ctrl+B` for full page
- **Page jump:** `gg` first page, `G` last page, `[n]gg` or `[n]G` go to page n
- **History:** `H` jump back, `L` jump forward — tracks page positions across jumps
- **Mouse scroll** supported in both terminal and Neovim modes

## Search

Vim-style search with MuPDF's text engine:

- Press `/` to enter search mode, type your query
- All matches are highlighted with a semi-transparent overlay
- `n`/`N` cycle through matches; the active match has a distinct highlight color
- Match count shown in the statusline (e.g., `[3/12]`)
- `Esc` clears the search

## Table of Contents

Two ways to browse the document outline:

- **Overlay** (`o` in terminal mode): centered popup showing the TOC, navigate with `j`/`k` and jump with `Enter`
- **Sidebar** (`e` in terminal mode): persistent left-side panel with the TOC
- **Telescope** (`o` in Neovim mode): fuzzy-find TOC entries via Telescope picker

## Link Hints

Press `f` to activate link hints — all links on visible pages get labeled with short letter sequences (like Vimium). Type the label to follow:

- **Internal links** jump to the target page within the document
- **External links** open in your default browser (via `xdg-open` or `open`)

Press `Esc` to cancel link hints.

## Visual Selection and Copy

Select text from rendered pages:

- `v` enters visual (character) selection mode
- `Ctrl+V` enters visual block selection mode
- Move the selection with `h`/`j`/`k`/`l`, `w`/`b` (word motions), `0`/`$`/`^` (line motions), `e` (word end), `gg`/`G` (document start/end)
- `y` yanks the selected text to the clipboard via OSC 52
- `Esc` cancels the selection

The selection is highlighted as a semi-transparent overlay composited onto the page.

## Themes

| Theme | Description |
|-------|-------------|
| **dark** | Dark background with light text recoloring |
| **light** | Light background with dark text recoloring |
| **auto** | Detects terminal background (via OSC 10/11) and picks dark or light |
| **terminal** | Recolors document using your terminal's actual foreground/background colors |

Toggle with `t` at runtime. The terminal theme uses configurable `recolor-dark`, `recolor-light`, and `recolor-accent` colors to remap document colors to match your color scheme.

## Customization

- **Keybindings:** every action can be rebound in the `[keys]` config section — see [[Configuration]]
- **Colors:** statusline, overlay, sidebar, search highlights, link hints, and recoloring colors are all configurable in the `[colors]` section
- **Config file:** `~/.config/mupager/config.toml` — see [[Configuration]] for the full reference

## Document Conversion and Watch Mode

mupager can convert non-native formats (like Markdown) to PDF using external tools, and watch the source file for changes to auto-reconvert and reload. See [[Converters and Watch Mode]] for details.

## Two Frontend Modes

- **[[Terminal Mode]]** — standalone TUI with built-in statusline, overlays, sidebar, command mode, and help screen
- **[[Neovim Mode]]** — runs as a Neovim plugin backend, integrating with Neovim's input, statusline, and Telescope
