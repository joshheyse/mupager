# Project Plan

## Completed

| Feature                 | Notes                                                                                                  |
| ----------------------- | ------------------------------------------------------------------------------------------------------ |
| Document loading        | PDF, EPUB, XPS, CBZ/CBR, FB2, MOBI, SVG, HTML, images via MuPDF                                        |
| Page rendering          | MuPDF render → Pixmap → PNG encode → Kitty transmit                                                    |
| Kitty graphics protocol | Full command set: transmit, place, delete, animation, compose                                          |
| Tmux support            | DCS passthrough, unicode placeholders, per-row cursor positioning                                      |
| View modes              | Continuous, page-width, page-height, side-by-side (Tab cycles)                                         |
| Navigation              | j/k scroll, d/u half-page, Ctrl+F/B page, h/l horizontal, gg/G with [n] count prefix                   |
| Dark mode               | Pixel inversion on rendered pages, `t` toggles                                                         |
| Statusbar               | Left (action flash/input) + right (mode, theme, page/total), reverse video, UTF-8 column-aware padding |
| Help overlay            | `?` shows centered popup, auto-generated from KeyBinding table                                         |
| Page cache              | Upload/evict by distance from viewport, idle pre-upload of adjacent pages                              |
| CLI args                | file (positional), --view-mode, --log-level, --log-file                                                |
| Build system            | CMake + CPM (doctest, CLI11, spdlog, tomlplusplus) + Nix flake (mupdf, msgpack-cxx)                    |

## Roadmap

### 2. Zoom Controls

- `+`/`-` zoom in/out through discrete steps (0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0)
- `0` reset to fit-width
- Cache invalidation on zoom change
- Zoom level shown in statusbar

### 3. Command Input (`:` mode)

- `:` enters command mode, text input rendered in statusbar left section
- Commands: `goto <page>`, `set <option> <value>`, `quit`
- Enter executes, Escape cancels
- Tab completion (stretch)
- Uses existing `InputMode::COMMAND` and `command_input_` stubs

### 4. Table of Contents

- Parse via `fz_load_outline()` → flat list with `{title, page, level}`
- **Popup mode**: centered overlay (like help), `j`/`k` navigate, Enter jumps, `q`/Escape closes
- **Sidebar mode**: pinned left panel showing TOC with current page highlighted
- Toggle with a keybinding (e.g., `o` for outline)
- Gracefully skip for documents without TOC
- fzf style filtering down of TOC sections

### 5. Search with Highlighting

- `/` enters search mode, text input in statusbar
- `fz_search_page()` across all pages → collect `{page, fz_quad}` hits
- **Compositor**: alpha-blend semi-transparent highlight rects onto rendered pixmap before upload
  - All matches: yellow overlay
  - Active match: orange, higher opacity
- `n`/`N` cycle through matches, jump to page containing match
- Match count in statusbar (`3/42 matches`)
- Escape clears search
- Uses existing `InputMode::SEARCH`, `search_term_`, match count stubs

### 6. Visual Selection & Copy

- `v` enters visual mode, cursor tracks a position in page coordinates
- Movement keys (j/k/h/l) extend selection rectangle
- Selection overlay rendered via compositor (blue semi-transparent)
- `y` extracts text within selection via `fz_copy_selection()` → OSC 52 clipboard escape
- Escape exits visual mode
- Requires cursor position tracking in page coordinate space

### 7. Link Following

- `fz_load_links()` returns link annotations per page
- Visual indicators for link regions (underline or subtle highlight)
- Key to activate link under cursor (Enter or `gx`)
- Internal links: jump to target page/position
- External URLs: open via `$BROWSER` or `xdg-open`/`open`

### 8. Mouse Support

- Enable SGR mouse reporting (`\e[?1006h`) for click, drag, and scroll events
- Scroll wheel: vertical scroll (same as j/k), with Shift for horizontal
- Click: jump to page at click position, set cursor for visual mode
- Click-drag: start visual selection (equivalent to `v` + motion)
- Ctrl+click: follow link under cursor
- Mouse support toggleable via config (`mouse = true/false`)
- Coordinate translation: terminal cell → pixel → page coordinate space

### 9. Annotations & Bookmarks

- Bookmark current page (toggle with `m`, list with `'`)
- Highlight text regions (persistent)
- Add text notes to pages
- Store in JSON sidecar file alongside document (`<filename>.mupager.json`)
- Render bookmark/highlight indicators in viewport

### 10. Neovim Frontend

- `NeovimFrontend` implements `Frontend` interface
- Connect to `$NVIM` socket via msgpack-rpc
- Send Kitty escapes via `nvim_chan_send(1, data)`
- Receive input events via RPC notifications
- Query window dimensions via `nvim_win_get_width/height()`
- Lua plugin (`nvim/lua/mupager/`): launch server as job, keybinding forwarding, buffer lifecycle
- Auto-detect via `$NVIM` env var or `--nvim` flag

### 11. Config & Settings

- Load TOML config from `$XDG_CONFIG_HOME/mupager/config.toml`
- Settings: default theme, view mode, cache size, log level, colors (search highlight, selection, etc.)
- Keybinding overrides (key → command name mapping in `[keybinds]` section)
- CLI flags: `--dark`/`--light`, `--config`
- tomlplusplus already linked, just needs `config.h`/`config.cpp`

## Dependencies

| Library     | Purpose             | Source            |
| ----------- | ------------------- | ----------------- |
| MuPDF       | Document rendering  | Nix (pkg-config)  |
| msgpack-cxx | Neovim RPC          | Nix (header-only) |
| toml++      | Config file parsing | CPM (header-only) |
| CLI11       | Argument parsing    | CPM               |
| spdlog      | Structured logging  | CPM               |
| doctest     | Unit testing        | CPM               |

## Testing Strategy

Buffer-level unit tests — no visual regression. Trust MuPDF rendering; test logic we write.

| Layer      | What's tested                                          | Approach                                           |
| ---------- | ------------------------------------------------------ | -------------------------------------------------- |
| Document   | Open, page count, render, search, text extraction, TOC | Real MuPDF against `server/test/fixtures/test.pdf` |
| Pixmap     | RAII lifecycle, invert                                 | Synthetic + MuPDF buffers                          |
| Compositor | Alpha-blend math                                       | Small synthetic buffers, assert exact output       |
| Config     | Parse valid TOML, defaults, malformed                  | Parse from string literal                          |
| Search     | Hit collection, n/N cycling/wrapping                   | Real MuPDF search on fixture PDF                   |
| KeyMap     | Single keys, gg sequence, count prefix, unknown keys   | Feed InputEvents, assert Command output            |
| Kitty      | Encode, chunk, place, delete, tmux wrap                | Assert escape sequence output                      |
