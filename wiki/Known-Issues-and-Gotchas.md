# Known Issues and Gotchas

## Transparent Backgrounds in Kitty

**Issue:** Kitty treats cells with its exact default background color as "transparent" for z-index image placement. This means floating windows with the terminal's default bg will let images show through.

**Workaround:** mupager nudges the blue channel of float background colors by 1 unit so they never exactly match the terminal's default bg. This is invisible visually but prevents the transparency behavior. See [kitty #7563](https://github.com/kovidgoyal/kitty/issues/7563).

If you use a transparent Neovim theme, set `fallback_bg` to your terminal's actual background color:

```lua
require("mupager").setup({
  fallback_bg = 0x1a1b26,
})
```

## Floating Windows in Neovim

**Issue:** Floating windows from Telescope, noice.nvim, notification plugins, etc. can appear transparent over the document image.

**Workaround:** mupager automatically forces opaque backgrounds on common float highlight groups and runs a periodic timer (default 100ms) to catch newly created floating windows. This covers windows created with `noautocmd` that would be missed by autocmds.

The affected highlight groups include `NormalFloat`, `FloatBorder`, `TelescopeNormal`, `TelescopeBorder`, `NoiceCmdlinePopup`, and others. Original highlight values are restored when mupager closes.

## Tmux Window Switching

**Issue:** Kitty images persist at terminal-absolute coordinates across tmux window switches. When you switch to a different tmux window, the document image remains visible even though the pane is no longer active.

**Workaround:** mupager polls `#{window_active}` via `tmux display-message` at a configurable interval (default 100ms). When the window becomes inactive, images are hidden; when it becomes active again, they're shown.

This polling approach is more reliable than `FocusLost`/`FocusGained` events, which miss tmux keybinding-driven window switches.

## setjmp/longjmp vs C++ RAII

**Issue:** MuPDF uses `fz_try`/`fz_catch`/`fz_always` for error handling, which are implemented with `setjmp`/`longjmp` in C. This can bypass C++ destructors when an exception occurs, leading to resource leaks.

**Mitigation:** All MuPDF C objects are wrapped in RAII types that call `fz_drop_*` in destructors. Care is taken to ensure `fz_always` blocks clean up resources on every code path. Stack-allocated C++ objects (like `std::string`) should not be constructed before `fz_try` blocks.

## Large Documents and Memory

**Issue:** Documents with many pages or high-resolution rendering can consume significant memory.

**Mitigation:**
- The page cache has a configurable size limit (default 64 MB via `--max-page-cache`)
- Only visible pages and their immediate neighbors are rendered
- Use `--render-scale` to reduce rendering resolution (e.g., `0.5` for half resolution)
- Use `page` or `page-height` view mode instead of `continuous` to limit the number of simultaneously visible pages

## Terminal Color Detection

**Issue:** Auto-detection of terminal foreground/background colors via OSC 10/11 queries may fail in some environments (e.g., certain tmux configurations, SSH connections, or terminals that don't respond to these queries).

**Workaround:** Set colors explicitly in config.toml:

```toml
terminal-fg = "#c0caf5"
terminal-bg = "#1a1b26"
```

Or via CLI:

```bash
mupager --terminal-fg "#c0caf5" --terminal-bg "#1a1b26" document.pdf
```

In Neovim mode, the plugin automatically reads colors from the `Normal` highlight group and passes them to the server.

Run `mupager --diagnose` to see what colors were detected.

## See Also

- [[Troubleshooting]] — step-by-step solutions for common problems
- [[Requirements and Compatibility]] — supported environments
