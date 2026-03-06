# Render Pipeline

## Overview

mupager transforms a document file into terminal-displayed images through a multi-stage pipeline:

```
File → MuPDF → Pixmap → Theme → Highlights → Kitty → Display
```

The rendering core is shared between terminal and Neovim modes — only the final display stage differs.

## Stage 1: Document Loading

MuPDF opens the file using `fz_open_document`, which auto-detects the format (PDF, EPUB, XPS, CBZ, images, etc.) through a single unified API. No format-specific code is needed.

If a [[Converters and Watch Mode|converter]] is configured, the source file is first converted to PDF, and the resulting temp file is opened instead.

## Stage 2: Page Rendering

Each page is rendered to an in-memory pixmap (bitmap):

1. `fz_load_page` loads the page structure
2. A transformation matrix is computed from the current zoom level and render scale
3. `fz_new_pixmap_from_page` renders the page to RGB pixels at the target resolution

The render scale (`--render-scale`) controls the resolution of cached pages:
- `auto` — scales based on terminal cell pixel density
- `never` — renders at native document resolution
- `0.25` to `4` — explicit scale factors

## Stage 3: Theme Application

The raw pixmap colors are transformed based on the active theme:

| Theme | Behavior |
|-------|----------|
| `dark` | Inverts luminance — dark backgrounds become light, light text becomes dark |
| `light` | No color transformation (documents are typically white-on-black) |
| `auto` | Detects terminal background (via OSC 10/11) and applies dark or light |
| `terminal` | Remaps document colors using `recolor-dark`, `recolor-light`, and `recolor-accent` to match your terminal's color scheme |

Terminal foreground/background colors are detected automatically via OSC 10/11 queries, or can be overridden with `--terminal-fg` / `--terminal-bg`.

## Stage 4: Highlight Compositing

Overlays are alpha-blended onto the page pixmap before encoding:

- **Search highlights** — semi-transparent rectangles on all matches, with a distinct color for the active match
- **Selection highlights** — visual mode selection region
- **Link hint labels** — text labels rendered at link positions

The compositing uses per-pixel alpha blending:

```
pixel = pixel × (1 - alpha) + overlay × alpha
```

This is done directly on the pixmap data, so highlights are part of the final image — no separate overlay layer is needed in the terminal.

## Stage 5: Layout Computation

The layout engine determines which pages (or page regions) are visible based on the view mode:

| Mode | Layout |
|------|--------|
| `continuous` | Pages stacked vertically; the viewport can span multiple pages |
| `page` | One page centered on screen |
| `page-height` | One page scaled to fill terminal height |
| `side-by-side` | Two pages side by side |

The output is a list of `PageSlice` structures, each describing:
- **Source rect:** which portion of the page image is visible (pixels)
- **Destination rect:** where to display it on screen (cells)
- **Image ID:** the Kitty image ID for this page
- **Grid dimensions:** for tmux Unicode placeholder computation

## Stage 6: Kitty Encoding

Each visible page slice is transmitted to the terminal using the [[Kitty Graphics Protocol]]:

1. The pixmap is encoded as PNG (smaller over the wire) or raw RGB pixels
2. The encoded data is base64-encoded
3. Large payloads are split into 4096-byte chunks
4. Image IDs enable in-place replacement (no flicker on re-render)

In tmux, each escape sequence is wrapped in DCS passthrough, and Unicode placeholder characters are emitted for image positioning.

## Stage 7: Frontend Display

The final display path differs by frontend:

| Frontend | Output |
|----------|--------|
| **Terminal** | Kitty escapes written directly to stdout |
| **Neovim** | Kitty escapes sent via `nvim_chan_send(1, ...)` through the RPC channel (channel 1 = terminal stdout) |
| **Tmux** | Both paths wrap escapes in DCS passthrough + emit Unicode placeholders |

## Page Caching

Rendered pixmaps are cached in an LRU cache to avoid re-rendering pages that haven't changed:

- Default cache size: 64 MB (configurable via `--max-page-cache`)
- Cache key: page number + zoom level + render scale
- On scroll in continuous mode, nearby pages are pre-rendered and cached
- When only highlights change (search, selection), the cached base pixmap is reused and only the compositing step is re-run — this makes highlight updates very fast

## See Also

- [[Kitty Graphics Protocol]] — escape sequence reference
- [[Configuration]] — render-scale, theme, and cache settings
