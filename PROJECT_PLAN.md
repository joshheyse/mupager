# Implementation Plan

Build order: standalone TUI server first, then Neovim integration.

## Dependencies

| Library     | Purpose                                                       | Source            |
| ----------- | ------------------------------------------------------------- | ----------------- |
| MuPDF       | Document rendering                                            | Nix (pkg-config)  |
| msgpack-cxx | Neovim RPC                                                    | Nix (header-only) |
| notcurses   | TUI framework, Kitty graphics via `ncvisual` + `NCBLIT_PIXEL` | Nix (pkg-config)  |
| toml++      | Config file parsing                                           | CPM (header-only) |
| CLI11       | Argument parsing                                              | CPM (header-only) |
| spdlog      | Structured logging                                            | CPM               |
| doctest     | Unit testing                                                  | CPM               |

## Testing Strategy

Buffer-level unit tests only — no visual regression or golden image diffs. Trust MuPDF's rendering correctness; test the logic we write.

| Layer        | What's tested                                                                        | Approach                                                  |
| ------------ | ------------------------------------------------------------------------------------ | --------------------------------------------------------- |
| `Document`   | Open, page count, render returns valid pixmap, search returns quads, text extraction | Real MuPDF calls against `server/test/fixtures/test.pdf`  |
| `Pixmap`     | RAII lifecycle, `invert_luminance` changes values                                    | Synthetic + MuPDF-rendered buffers                        |
| `Compositor` | Alpha-blend math produces correct pixel values                                       | Small synthetic buffers (e.g. 10x10), assert exact output |
| `PageCache`  | LRU insert/get/evict, capacity limit, invalidation                                   | Pure data structure, no MuPDF needed                      |
| `KeyMap`     | Single keys, `gg` sequence, count prefix `5G`, unknown keys                          | Feed `InputEvent`s, assert `Command` output               |
| `Config`     | Parse valid TOML, defaults on missing fields, malformed file                         | Parse from string literal                                 |
| `Search`     | Hit collection across pages, `n`/`N` index cycling/wrapping                          | Real MuPDF search on fixture PDF with known text content  |

**Not unit tested** (validated by running the app): `TerminalFrontend`, `NeovimFrontend`, `App`, `KittyGraphics`.

Test fixture: a small multi-page PDF with known text (for search tests) committed at `server/test/fixtures/test.pdf`.

## Source Layout

```
server/src/
├── main.cpp                 # CLI parsing, App bootstrap
├── app.h / app.cpp          # Application lifecycle, main loop
├── document.h / .cpp        # MuPDF document wrapper
├── page_cache.h / .cpp      # LRU page pixmap cache
├── pixmap.h / .cpp          # RAII pixmap wrapper + compositing helpers
├── compositor.h / .cpp      # Overlay search highlights, selections onto pixmap
├── frontend.h               # Abstract frontend interface
├── terminal_frontend.h/.cpp # notcurses standalone TUI
├── neovim_frontend.h / .cpp # msgpack-rpc + raw Kitty escapes
├── kitty_graphics.h / .cpp  # Kitty graphics protocol encoder (for Neovim mode)
├── key_map.h / .cpp         # Vim-style keybinding dispatch
├── command.h                # Command enum / variant
├── config.h / .cpp          # TOML config loading
├── statusline.h / .cpp      # Statusline rendering
└── search.h / .cpp          # MuPDF text search + hit management
```

## Class Designs

### Document

RAII wrapper around MuPDF. Owns `fz_context` and `fz_document`.

```cpp
class Document {
public:
  explicit Document(const std::string& path);
  ~Document();

  int page_count() const;
  Pixmap render_page(int page_num, float zoom) const;

  // Text search — returns quads in page coordinates
  std::vector<fz_quad> search_page(int page_num, const std::string& needle) const;

  // Text extraction within a rect
  std::string extract_text(int page_num, fz_rect selection) const;

  // Table of contents
  struct TocEntry { std::string title; int page; int level; };
  std::vector<TocEntry> table_of_contents() const;

private:
  fz_context* ctx_;
  fz_document* doc_;
};
```

Key MuPDF calls:

- `fz_new_context(NULL, NULL, FZ_STORE_DEFAULT)` — create context
- `fz_register_document_handlers(ctx)` — enable all format handlers
- `fz_open_document(ctx, path)` — open any supported format
- `fz_count_pages(ctx, doc)` — page count
- `fz_load_page(ctx, doc, n)` / `fz_drop_page(ctx, page)` — page lifecycle
- `fz_new_pixmap_from_page(ctx, page, ctm, colorspace, alpha)` — render
- `fz_search_page(ctx, page, needle, NULL, hits, max_hits)` — text search
- `fz_new_stext_page_from_page(ctx, page, NULL)` — structured text for extraction
- `fz_load_outline(ctx, doc)` — table of contents tree

### Pixmap

RAII wrapper around `fz_pixmap`. Exposes raw pixel buffer for compositing.

```cpp
class Pixmap {
public:
  Pixmap(fz_context* ctx, fz_pixmap* pix); // takes ownership
  ~Pixmap();
  Pixmap(Pixmap&& other) noexcept;

  int width() const;
  int height() const;
  int stride() const;
  int components() const; // 3 for RGB, 4 for RGBA
  unsigned char* samples();
  const unsigned char* samples() const;

  // Invert for dark mode
  void invert_luminance();

private:
  fz_context* ctx_;
  fz_pixmap* pix_;
};
```

Dark mode: `fz_invert_pixmap_luminance(ctx, pix)` — inverts luminance while preserving hue. Better than simple RGB inversion for documents with colored elements.

### PageCache

LRU cache mapping `(page_num, zoom_level)` → `Pixmap`. Evicts least-recently-used entries when capacity exceeded.

```cpp
class PageCache {
public:
  explicit PageCache(size_t capacity = 8);

  // Returns cached pixmap or nullptr
  Pixmap* get(int page_num, float zoom);

  // Insert (may evict)
  void put(int page_num, float zoom, Pixmap pixmap);

  void invalidate_all();

private:
  struct Key { int page; float zoom; };
  size_t capacity_;
  std::list<std::pair<Key, Pixmap>> items_;
  // + hash map for O(1) lookup
};
```

### Compositor

Alpha-blends overlays (search highlights, selection, cursor) onto a pixmap copy before display. Never mutates the cached pixmap.

```cpp
class Compositor {
public:
  // Clone pixmap and composite overlays onto it
  Pixmap compose(fz_context* ctx, const Pixmap& base,
                 const std::vector<Overlay>& overlays) const;

private:
  void blend_rect(unsigned char* pixels, int img_w, int img_h,
                  fz_rect rect, Color color, uint8_t alpha) const;
};

struct Overlay {
  fz_rect rect;
  Color color;
  uint8_t alpha;
};
```

### Frontend (abstract)

```cpp
class Frontend {
public:
  virtual ~Frontend() = default;

  // Display a rendered pixmap at the given position
  virtual void display(const Pixmap& pixmap, int x, int y) = 0;

  // Get terminal dimensions in pixels
  virtual std::pair<int, int> pixel_size() const = 0;

  // Get terminal dimensions in cells
  virtual std::pair<int, int> cell_size() const = 0;

  // Clear the display
  virtual void clear() = 0;

  // Poll for input events (blocking with timeout)
  virtual std::optional<InputEvent> poll_input(int timeout_ms) = 0;

  // Render statusline text
  virtual void statusline(const std::string& text) = 0;
};
```

### TerminalFrontend

notcurses-based standalone TUI. Uses `ncvisual` + `NCBLIT_PIXEL` for Kitty graphics.

```cpp
class TerminalFrontend : public Frontend {
public:
  TerminalFrontend();
  ~TerminalFrontend() override;

  void display(const Pixmap& pixmap, int x, int y) override;
  std::pair<int, int> pixel_size() const override;
  std::pair<int, int> cell_size() const override;
  void clear() override;
  std::optional<InputEvent> poll_input(int timeout_ms) override;
  void statusline(const std::string& text) override;

private:
  struct notcurses* nc_;
  struct ncplane* std_plane_;
  struct ncvisual* visual_;  // reused for page display
};
```

Key notcurses calls:

- `notcurses_init(NULL, stdout)` — initialize
- `notcurses_term_dim_yx(nc)` — cell dimensions
- `ncplane_pixel_geom(plane, ...)` — pixel dimensions
- `ncvisual_from_rgba(data, rows, stride, cols)` — create visual from raw RGBA
- `ncvisual_blit(nc, visual, &vopts)` with `vopts.blitter = NCBLIT_PIXEL` — pixel-perfect blit
- `notcurses_get(nc, &timeout, &input)` — blocking input with timeout
- `notcurses_render(nc)` — flush to terminal

### NeovimFrontend (later phase)

Connects to `$NVIM` socket, uses raw Kitty graphics escapes via `nvim_chan_send`.

### KeyMap

Maps input events to commands. Supports vim-style key sequences.

```cpp
class KeyMap {
public:
  KeyMap();

  // Feed an input event, returns a command if a binding matched
  std::optional<Command> feed(const InputEvent& event);

  // Load custom bindings from config
  void load(const Config& config);

private:
  // Single-key bindings
  std::unordered_map<uint32_t, Command> bindings_;

  // Multi-key sequences (gg, etc.)
  // Pending state for partial matches
  std::optional<uint32_t> pending_;
};
```

### Config

```cpp
struct Config {
  float default_zoom = 1.0f;
  bool dark_mode = false;
  int cache_size = 8;
  std::string log_level = "info";

  struct Colors {
    uint32_t search_highlight = 0xFFFF00;
    uint8_t search_alpha = 80;
    uint32_t active_highlight = 0xFF8800;
    uint8_t active_alpha = 120;
    uint32_t selection = 0x4488FF;
    uint8_t selection_alpha = 80;
  } colors;

  // Key overrides (string -> command name)
  std::unordered_map<std::string, std::string> keybinds;

  static Config load(const std::string& path);
  static Config defaults();
};
```

Config file location: `$XDG_CONFIG_HOME/mupager/config.toml` (fallback `~/.config/mupager/config.toml`).

Example:

```toml
default_zoom = 1.0
dark_mode = true
cache_size = 12
log_level = "debug"

[colors]
search_highlight = 0xFFFF00
search_alpha = 80
active_highlight = 0xFF8800
active_alpha = 120
selection = 0x4488FF
selection_alpha = 80

[keybinds]
q = "quit"
j = "scroll_down"
k = "scroll_up"
```

### App

Top-level orchestrator. Owns Document, PageCache, Frontend, Config. Runs the main loop.

```cpp
class App {
public:
  App(std::unique_ptr<Frontend> frontend, Config config, const std::string& file);

  void run(); // main loop

private:
  void handle_command(Command cmd);
  void render();  // render current view + overlays

  std::unique_ptr<Frontend> frontend_;
  Document doc_;
  PageCache cache_;
  Compositor compositor_;
  KeyMap key_map_;
  Config config_;

  // View state
  int current_page_ = 0;
  float zoom_ = 1.0f;
  int scroll_y_ = 0;
  bool dark_mode_ = false;

  // Search state
  std::string search_query_;
  std::vector<SearchHit> search_hits_;
  int search_index_ = -1;

  // Selection state
  std::optional<fz_point> cursor_;
  std::optional<fz_rect> selection_;
};
```

## Implementation Phases

### Phase 1: Document Loading

**Goal:** Open a document, log page count, exit.

**Files:** `document.h`, `document.cpp`, `document.test.cpp`, `main.cpp`

**MuPDF API:**

```cpp
fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
fz_register_document_handlers(ctx);
fz_document* doc = fz_open_document(ctx, path);
int pages = fz_count_pages(ctx, doc);
```

**Work:**

- Implement `Document` class (constructor, destructor, `page_count()`)
- CLI parsing with CLI11 (file positional arg)
- spdlog initialization
- Unit test: open a test PDF, assert page count > 0

**Test:** Ship a small test PDF in `server/test/fixtures/`. Doctest test opens it and checks page count.

---

### Phase 2: Page Rendering

**Goal:** Render a page to a `Pixmap`, verify pixel data is valid.

**Files:** `pixmap.h`, `pixmap.cpp`, `pixmap.test.cpp`, `document.cpp` (add `render_page`)

**MuPDF API:**

```cpp
fz_page* page = fz_load_page(ctx, doc, page_num);
fz_matrix ctm = fz_scale(zoom, zoom);
fz_pixmap* pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);
// pix->samples = RGB data, pix->w, pix->h, pix->stride
fz_drop_page(ctx, page);
```

**Work:**

- Implement `Pixmap` RAII wrapper
- `Document::render_page(page_num, zoom)` → `Pixmap`
- Unit test: render page 0, check `width() > 0 && height() > 0`, check samples non-null

---

### Phase 3: notcurses TUI Shell

**Goal:** Initialize notcurses, show a blank screen, handle `q` to quit, handle terminal resize.

**Files:** `terminal_frontend.h`, `terminal_frontend.cpp`, `frontend.h`, `command.h`, `app.h`, `app.cpp`, `main.cpp`

**notcurses API:**

```cpp
struct notcurses_options opts = {};
struct notcurses* nc = notcurses_init(&opts, stdout);
struct ncplane* std = notcurses_stdplane(nc);

// Input loop
struct ncinput ni;
uint32_t key = notcurses_get(nc, NULL, &ni); // blocking

// Cleanup
notcurses_stop(nc);
```

**Work:**

- Implement `TerminalFrontend` (init, input polling, cleanup)
- Define `Command` enum (`Quit`, `ScrollDown`, `ScrollUp`, etc.)
- Implement `App` skeleton with main loop: poll input → dispatch command → render
- `q` quits cleanly

---

### Phase 4: Display Page via ncvisual

**Goal:** Render page 1 and display it in the terminal using pixel-perfect blitting.

**Files:** `terminal_frontend.cpp` (implement `display`), `app.cpp` (call render on startup)

**notcurses API:**

```cpp
// Create visual from raw pixel data (RGBA required by ncvisual)
struct ncvisual* ncv = ncvisual_from_rgba(
    rgba_data, height, stride, width);

struct ncvisual_options vopts = {};
vopts.blitter = NCBLIT_PIXEL;  // Kitty graphics protocol
vopts.n = notcurses_stdplane(nc);
vopts.flags = NCVISUAL_OPTION_CHILDPLANE;

struct ncplane* rendered = ncvisual_blit(nc, ncv, &vopts);
notcurses_render(nc);

ncvisual_destroy(ncv);
```

**Work:**

- Convert MuPDF RGB pixmap to RGBA (add alpha channel) for ncvisual
- Implement `TerminalFrontend::display()` using `ncvisual_from_rgba` + `NCBLIT_PIXEL`
- Scale rendered page to fit terminal pixel dimensions
- Handle terminal resize: re-query `ncplane_pixel_geom`, re-render

**Note:** `ncplane_pixel_geom(std_plane, &pix_y, &pix_x, &cell_y, &cell_x, NULL, NULL)` gives terminal pixel dimensions and cell pixel size, needed to compute fit-width zoom.

---

### Phase 5: Page Navigation

**Goal:** `j`/`k` scroll, `d`/`u` half-page, `gg`/`G` first/last, `{n}G` go-to-page.

**Files:** `key_map.h`, `key_map.cpp`, `app.cpp` (handle navigation commands)

**Work:**

- Implement `KeyMap` with default vim bindings
- Handle count prefix for `{n}G`
- `gg` requires multi-key sequence detection (timeout-based or immediate on second `g`)
- Scrolling within a page (partial view for zoomed pages)
- Page transitions when scrolling past page boundary
- Re-render on navigation

---

### Phase 6: Page Cache

**Goal:** Cache rendered pixmaps to avoid re-rendering on revisit.

**Files:** `page_cache.h`, `page_cache.cpp`, `page_cache.test.cpp`

**Work:**

- LRU cache with configurable capacity
- Key: `(page_num, zoom_level)` — zoom as float, quantized to 2 decimal places for cache key stability
- Pre-render adjacent pages in background (page ± 1)
- Invalidate on zoom change
- Unit test: insert/get/eviction behavior

---

### Phase 6a: PNG support

Yes, PNG will be significantly more compact for PDF pages with mostly white backgrounds. PNG uses deflate internally, and it also applies per-row filters (sub, up, average, Paeth) before compression that exploit spatial redundancy. A mostly-white PDF page will compress extremely well since PNG's filtering + deflate will reduce large uniform regions to almost nothing.
So your options from most to least compact for this workload:

PNG (f=100) — best for your case. The filtering step is what gives PNG an edge over raw deflate on image data. Mostly-white PDFs will see massive compression ratios.
Raw RGBA + zlib (f=32,o=z) — deflate without row filters. Still good but won't exploit vertical redundancy as well as PNG.
Raw RGBA (f=32) — worst. A 1920×1080 RGBA page is ~8MB uncompressed, then base64 inflates it another 33%.

Go with PNG. You're already decoding the PDF to a raster — just encode that to PNG in memory and send it with f=100. For mostly-white document pages you'll likely see 10-50x size reduction over raw RGBA.

### Phase 7: Zoom

**Goal:** `+`/`-` zoom in/out, `0` fit-width.

**Files:** `app.cpp`

**Work:**

- Zoom steps: 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0
- `0` computes fit-width: `zoom = terminal_pixel_width / page_width_at_zoom_1`
- Zoom changes invalidate cache entries at old zoom level
- Re-render at new zoom, centered on current view position

---

### Phase 8: Dark Mode

**Goal:** `--dark` flag and `d` toggle for dark mode rendering.

**Files:** `pixmap.cpp` (add `invert_luminance`), `app.cpp`

**MuPDF API:**

```cpp
fz_invert_pixmap_luminance(ctx, pix);
```

**Work:**

- `Pixmap::invert_luminance()` calls MuPDF's luminance inversion
- Apply after rendering, before compositing overlays
- Dark mode pixmaps cached separately (dark mode flag in cache key)
- Toggle with runtime keybind

---

### Phase 9: Search

**Goal:** `/pattern` search, `n`/`N` cycle results, highlighted matches.

**Files:** `search.h`, `search.cpp`, `search.test.cpp`, `app.cpp`

**MuPDF API:**

```cpp
fz_quad hits[512];
int count = fz_search_page(ctx, page, needle, NULL, hits, 512);
// Each hit: hits[i].ul, hits[i].ur, hits[i].ll, hits[i].lr (quad corners)
// Convert to rect: fz_rect_from_quad(hits[i])
```

**Work:**

- Search all pages, collect hits as `{page, fz_rect}`
- `n` / `N` cycle through hits, jump to hit's page
- Enter search mode with `/`, read text input, exit with Enter/Escape
- Store hits for overlay rendering

---

### Phase 10: Compositing & Overlays

**Goal:** Render search highlights and selection as semi-transparent overlays on the page.

**Files:** `compositor.h`, `compositor.cpp`, `compositor.test.cpp`

**Work:**

- `Compositor::compose()` clones the cached pixmap and blends overlays
- Search hits: yellow semi-transparent rects
- Active hit: orange, higher opacity
- Selection: blue semi-transparent rect
- Alpha blending per pixel:
  ```
  out = (src * (255 - alpha) + overlay * alpha) / 255
  ```
- Overlay rects are in page coordinates, transformed by current zoom/scroll
- Unit test: blend onto a known pixel buffer, verify output values

---

### Phase 11: Cursor, Selection & Yank

**Goal:** Arrow keys / mouse move cursor, shift+move creates selection, `y` copies text.

**Files:** `app.cpp`, `document.cpp` (add `extract_text`)

**MuPDF API:**

```cpp
// Structured text for extraction
fz_stext_page* stext = fz_new_stext_page_from_page(ctx, page, NULL);
// Walk stext blocks/lines/chars within selection rect
// Or use: fz_copy_selection(ctx, stext, a, b, 0) -> char*
fz_drop_stext_page(ctx, stext);
```

**Work:**

- Track cursor position in page coordinates
- Shift+arrow creates/extends selection rect
- `v` enters visual mode (selection follows cursor movement)
- `y` extracts text within selection via MuPDF structured text API
- Yank to system clipboard (`OSC 52` escape sequence)
- Render cursor as thin vertical line overlay, selection as rect overlay

---

### Phase 12: Statusline & Config

**Goal:** Bottom statusline showing state. TOML config file.

**Files:** `statusline.h`, `statusline.cpp`, `config.h`, `config.cpp`, `config.test.cpp`

**Work:**

- Statusline: `page {n}/{total} | {zoom}% | {search_count} matches | {filename}`
- Render via notcurses text on the bottom row (below the page image)
- Config: load from `$XDG_CONFIG_HOME/mupager/config.toml`
- Config test: parse a sample TOML string, verify fields

---

### Phase 13: Table of Contents

**Goal:** Extract and display document TOC, jump to selected entry.

**Files:** `document.cpp` (add `table_of_contents`), `app.cpp`

**MuPDF API:**

```cpp
fz_outline* outline = fz_load_outline(ctx, doc);
// Walk tree: outline->title, outline->page, outline->next, outline->down
fz_drop_outline(ctx, outline);
```

**Work:**

- `Document::table_of_contents()` walks the outline tree, returns flat list with indent levels
- `t` opens a TOC overlay (rendered as text list on a semi-transparent background)
- Arrow keys / `j`/`k` navigate, Enter jumps to selected page, `Escape` closes
- Skip for documents without TOC

---

### Phase 14: Neovim Integration

**Goal:** Alternative frontend that communicates via msgpack-rpc over `$NVIM` socket.

**Files:** `neovim_frontend.h`, `neovim_frontend.cpp`, `kitty_graphics.h`, `kitty_graphics.cpp`

**Work:**

- `KittyGraphics` encoder: generate raw `\x1b_G...` escape sequences
  - Chunked base64 encoding (4096-byte chunks, `m=1`/`m=0`)
  - Image IDs for in-place replacement (no flicker)
  - Support RGB (`f=24`) and PNG (`f=100`) formats
- `NeovimFrontend`: connect to `$NVIM` Unix socket
  - Send Kitty escapes via `nvim_chan_send(1, data)`
  - Receive input events from Neovim via RPC notifications
  - Query window dimensions via `nvim_win_get_width/height()`
- Lua plugin (`nvim/lua/mupager/`):
  - Launch server as a Neovim job
  - Set up keybinding forwarding
  - Handle buffer lifecycle (open/close)
- CLI flag `--nvim` to select `NeovimFrontend` (auto-detected via `$NVIM` env var)

## Build Changes

### CMakeLists.txt (root)

Add CPM packages:

```cmake
CPMAddPackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  GIT_TAG v3.4.0
  SYSTEM TRUE
)

CPMAddPackage(
  NAME CLI11
  GITHUB_REPOSITORY CLIUtils/CLI11
  GIT_TAG v2.4.2
  SYSTEM TRUE
)

CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG v1.15.0
  SYSTEM TRUE
)
```

### flake.nix

Add to dev shell packages:

```nix
notcurses
```

### server/CMakeLists.txt

Add notcurses via pkg-config, link new CPM targets:

```cmake
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses++)
target_link_libraries(mupager
  PRIVATE PkgConfig::MUPDF
  PRIVATE PkgConfig::NOTCURSES
  PRIVATE tomlplusplus::tomlplusplus
  PRIVATE CLI11::CLI11
  PRIVATE spdlog::spdlog
)
```
