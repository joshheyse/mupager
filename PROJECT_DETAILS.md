# mupdf.nvim: Terminal Document Viewer

## Architecture

A document rendering server powered by MuPDF that displays pages in a Kitty-compatible terminal using the Kitty graphics protocol. The server supports two frontend modes:

1. **Neovim mode** — runs as a Neovim plugin backend, receiving input via msgpack-rpc and sending Kitty escapes through Neovim's terminal channel
2. **Standalone mode** — runs as a direct TUI, reading input from stdin and writing Kitty escapes to stdout

The rendering core, compositing, caching, and document handling are shared between both modes. Only the input/output layer differs.

### Supported Formats

MuPDF natively supports all of these through the same `fz_open_document` / `fz_load_page` / `fz_new_pixmap_from_page` pipeline — no format-specific code needed:

- **PDF**
- **EPUB**
- **XPS / OpenXPS**
- **CBZ / CBR** (comic book archives)
- **FB2**, **MOBI** (ebooks)
- **SVG**
- **HTML / XHTML**
- **Images** (PNG, JPG, BMP, etc.)

### Diagrams

**Neovim mode:**
```
+------------+       RPC (Unix socket)       +----------------+
|   Neovim   | <---------------------------> |  Doc Server    |
|            |   keybinds/mouse -> commands   |   (C++)        |
|  buffer =  |   <-- kitty image escapes     |                |
|  "canvas"  |                               |  libmupdf      |
+------------+                               +----------------+
      |                                             |
      v                                             v
  Kitty terminal                          rendering + compositing
  (displays images via graphics protocol)
```

**Standalone mode:**
```
+-------------------+
|    Doc Server     |
|      (C++)        |
|                   |
|  stdin -> input   |
|  stdout -> kitty  |
|  libmupdf         |
+-------------------+
         |
         v
   Kitty terminal
```

### Frontend Interface

The server defines an abstract frontend interface with two implementations:

- **NeovimFrontend** — connects to `$NVIM` socket via msgpack-rpc, receives input events, sends Kitty escapes via `nvim_chan_send(1, ...)`
- **TerminalFrontend** — reads keyboard/mouse input directly from the terminal, writes Kitty escapes to stdout

### Communication (Neovim mode)

- Server connects to Neovim's RPC socket (`$NVIM` env var, available inside Neovim jobs)
- Neovim sends user input events (keys, mouse) to the server via RPC
- Server sends Kitty graphics protocol escapes to the terminal via `nvim_chan_send(1, ...)` (channel 1 = terminal stdout)
- Kitty image protocol supports in-place image replacement (no flicker)

### Rendering Pipeline

1. **MuPDF** renders a specific page to a bitmap in memory (no disk I/O)
2. **Compositing** overlays search highlights, cursor, selections onto the bitmap
3. **Kitty graphics protocol** transmits the bitmap to the terminal (`\x1b_G...` escapes)
4. Only visible page(s) are rendered; nearby pages cached

## Features

### Core

- Open documents (PDF, EPUB, XPS, CBZ/CBR, FB2, MOBI, SVG, HTML, images)
- Page navigation (scroll, jump to page, `gg`/`G`)
- Zoom in/out
- Render only visible pages, cache neighbors
- Responsive to terminal resize

### Search

- Vim-style `/search text` triggers MuPDF text search
- MuPDF API returns `(page, x, y, w, h)` bounding boxes per match
- Render semi-transparent highlight rects composited onto the page bitmap
- `n`/`p` cycle through matches (focused match gets distinct highlight color)
- Jump to page containing focused match
- Search result count in statusline

### Cursor and Selection

- Track cursor position in PDF coordinate space
- Keyboard/mouse movement updates cursor position
- Shift+movement creates a selection range
- Selection highlight composited as overlay on the bitmap
- Yank (`y`) copies selected text via MuPDF text extraction within selection rect
- Text goes into Neovim's registers via RPC

### Table of Contents

- Extract TOC from PDF metadata via MuPDF
- Display in a Neovim split/float (telescope/fzf picker)
- Jump to selected heading

## Technical Details

### MuPDF C API

```c
// Open document
fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
fz_document *doc = fz_open_document(ctx, "file.pdf");

// Render a page to pixmap (in-memory bitmap)
fz_page *page = fz_load_page(ctx, doc, page_num);
fz_matrix ctm = fz_scale(zoom, zoom);  // zoom factor
fz_pixmap *pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);

// Access raw pixel data
unsigned char *samples = fz_pixmap_samples(ctx, pix);
int w = fz_pixmap_width(ctx, pix);
int h = fz_pixmap_height(ctx, pix);
// samples is RGB, 3 bytes per pixel, row-major

// Text search
fz_quad hits[MAX_HITS];
int n = fz_search_page(ctx, page, "search text", NULL, hits, MAX_HITS);
// each hit is a quad with (ul, ur, ll, lr) corner coordinates
```

### Kitty Graphics Protocol

```
# Transmit + display a PNG image (base64 encoded)
\x1b_Gf=100,a=T,t=d;{base64_data}\x1b\\

# Transmit raw RGBA pixels
\x1b_Gf=32,s={width},v={height},a=T,t=d;{base64_data}\x1b\\

# Replace an existing image (assign ID, then update)
\x1b_Gi={id},f=32,s={width},v={height},a=T,t=d;{base64_data}\x1b\\

# Delete image by ID
\x1b_Ga=d,d=i,i={id}\x1b\\
```

Key points:
- Use image IDs for in-place replacement (avoids flicker)
- Large images must be chunked (4096 byte base64 chunks, `m=1` for more data, `m=0` for last chunk)
- `f=32` = RGBA, `f=24` = RGB, `f=100` = PNG
- PNG is smaller over the wire; raw pixels skip encode/decode

### Neovim RPC (msgpack-rpc)

- Connect to `$NVIM` socket using msgpack-rpc
- Libraries: msgpack-c (C/C++), or hand-roll the simple protocol
- Key API calls:
  - `nvim_chan_send(1, data)` - send raw bytes to terminal
  - `nvim_buf_set_keymap()` - register keybindings
  - `nvim_create_autocmd()` - listen for events (resize, buffer enter/leave)
  - `nvim_win_get_width/height()` - get terminal cell dimensions for sizing
  - `nvim_set_var()` / `nvim_get_var()` - share state

### Compositing Overlays

For search highlights and selections, composite directly onto the MuPDF pixmap before sending to Kitty:

```cpp
// Alpha-blend a highlight rect onto the pixmap
void overlay_rect(unsigned char *pixels, int img_w, int img_h,
                  int rx, int ry, int rw, int rh,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    for (int y = ry; y < ry + rh && y < img_h; y++) {
        for (int x = rx; x < rx + rw && x < img_w; x++) {
            int i = (y * img_w + x) * 3;
            pixels[i]     = (pixels[i]     * (255 - alpha) + r * alpha) / 255;
            pixels[i + 1] = (pixels[i + 1] * (255 - alpha) + g * alpha) / 255;
            pixels[i + 2] = (pixels[i + 2] * (255 - alpha) + b * alpha) / 255;
        }
    }
}
```

## Build

### Language: C++17

The server is implemented in C++. This was chosen over Rust and Go because:
- **MuPDF is a C library** — C++ wraps it with zero overhead and no FFI bindings layer
- **Pixel-level compositing** (alpha-blending overlays onto pixmaps) is naturally pointer-arithmetic-on-buffers work; Rust would require `unsafe` blocks for the same operations, and Go lacks low-level memory control
- **Small, focused codebase** — the memory-safety advantages of Rust are strongest in large codebases with complex ownership; this server has a tight, auditable scope
- **CGo overhead** rules out Go — MuPDF rendering is call-heavy, and CGo has real per-call cost (goroutine stack switching, no inlining across the boundary)

Use C++17 features: `std::variant` for RPC message dispatch, `std::span` for buffer views, structured bindings, `std::optional`, etc.

### Dependencies

- `libmupdf` - document rendering (C library, link against `-lmupdf -lmupdf-third`)
- `msgpack-c` or `msgpack-cxx` - Neovim RPC protocol
- Kitty terminal (or any terminal supporting the Kitty graphics protocol)

### Nix

All dependencies available in nixpkgs:
- `pkgs.mupdf` (library + headers)
- `pkgs.msgpack-cxx`

## Vim Keybinding Plan

| Key | Action |
|-----|--------|
| `j`/`k` | Scroll down/up |
| `d`/`u` | Half-page down/up |
| `gg`/`G` | First/last page |
| `{n}G` | Go to page n |
| `+`/`-` | Zoom in/out |
| `0` | Reset zoom to fit-width |
| `/pattern` | Search forward |
| `n`/`N` | Next/previous search result |
| `v` | Enter visual/selection mode |
| `y` | Yank selected text |
| `q` | Quit |
| `t` | Open table of contents picker |

## Open Questions

- Continuous scroll (render partial pages) vs page-at-a-time?
- Dual-page spread mode?
- Annotation support (highlights persisted to sidecar file)?
- Link following (clickable URLs and internal refs)?
