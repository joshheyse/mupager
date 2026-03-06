# Troubleshooting

## Document Doesn't Render

1. **Check your terminal** — mupager requires Kitty, WezTerm, or Ghostty. See [[Requirements and Compatibility]].
2. **Run diagnostics:**
   ```bash
   mupager --diagnose
   ```
   Check that the terminal is detected and Kitty graphics support is confirmed.
3. **Check logs:**
   ```bash
   mupager --log-level debug document.pdf
   ```
   Or in Neovim: `:MupagerLog`

## Blank Screen in Tmux

Tmux blocks Kitty graphics escapes by default.

1. Add to your `tmux.conf`:
   ```bash
   set -g allow-passthrough on
   ```
2. **Restart tmux** (not just reload config — passthrough requires a full restart)
3. Verify with `mupager --diagnose` that tmux passthrough is detected

## Images Visible Through Floating Windows

This is handled automatically in Neovim mode. If you still see issues:

1. The periodic timer (100ms) should catch new floating windows. If a specific plugin's windows are not covered, check `:MupagerLog` for details.
2. For transparent Neovim themes, set `fallback_bg`:
   ```lua
   require("mupager").setup({
     fallback_bg = 0x1a1b26,  -- your terminal's actual bg
   })
   ```
3. See [[Known Issues and Gotchas]] for the Kitty #7563 background.

## Server Fails to Start

1. **Check PATH:** ensure `mupager` is in your `$PATH`:
   ```bash
   which mupager
   ```
2. **Set explicit path** in Neovim:
   ```lua
   require("mupager").setup({
     bin = "/path/to/mupager",
   })
   ```
3. **Check logs:** `:MupagerLog` in Neovim shows server stderr output and exit codes

## Wrong Colors or Theme

1. **Try auto theme:**
   ```bash
   mupager --theme auto document.pdf
   ```
2. **Check detected colors:**
   ```bash
   mupager --diagnose
   ```
   Look at the "Colors" line for detected fg/bg.
3. **Override manually** if detection fails:
   ```toml
   terminal-fg = "#c0caf5"
   terminal-bg = "#1a1b26"
   ```
4. **Use terminal theme** for color-scheme-matched rendering:
   ```toml
   theme = "terminal"

   [colors]
   recolor-dark = "#c0caf5"
   recolor-light = "#1a1b26"
   recolor-accent = "#7aa2f7"
   ```

## Converter Not Running

1. **Check pattern matching** — patterns use glob syntax (`*.md`, not `*.markdown`). The pattern must match the filename:
   ```toml
   [converters]
   "*.md" = "pandoc %i -o %o"
   ```
2. **Check the converter tool is in PATH:**
   ```bash
   which pandoc
   ```
3. **Enable debug logging** to see converter execution:
   ```bash
   mupager --log-level debug --converter "pandoc %i -o %o" notes.md
   ```
4. **CLI override** takes priority over config patterns — use `--converter` to test directly

## Slow Rendering or High Memory

1. **Reduce cache size:**
   ```bash
   mupager --max-page-cache 32 document.pdf
   ```
2. **Lower render scale:**
   ```bash
   mupager --render-scale 0.5 document.pdf
   ```
3. **Use page mode** instead of continuous for large documents:
   ```bash
   mupager --view-mode page document.pdf
   ```
4. **Check cache stats** to understand memory usage:
   ```bash
   mupager --show-stats document.pdf
   ```

## Neovim: Buffer Opens as Binary

If PDF files open as raw binary instead of rendering:

1. **Check setup timing** — ensure `require("mupager").setup()` is called before opening PDF files
2. **Check file patterns:**
   ```lua
   require("mupager").setup({
     patterns = { "*.pdf", "*.epub" },  -- explicit patterns
   })
   ```
3. **Verify the autocmd exists:**
   ```vim
   :autocmd BufReadCmd
   ```
   You should see a `mupager_filetype` group entry.
4. **Try opening manually:**
   ```vim
   :MupagerOpen /path/to/document.pdf
   ```

## See Also

- [[Known Issues and Gotchas]] — platform quirks and workarounds
- [[Requirements and Compatibility]] — supported environments and diagnostics
