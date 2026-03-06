# Requirements and Compatibility

## Terminal Requirements

mupager requires a terminal that supports the [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/):

| Terminal | Status | Notes |
|----------|--------|-------|
| **Kitty** | Full support | Native graphics protocol |
| **WezTerm** | Full support | Kitty graphics protocol compatible |
| **Ghostty** | Full support | Kitty graphics protocol compatible |
| **iTerm2** | Not supported | Uses a different image protocol |
| **Alacritty** | Not supported | No graphics protocol support |
| **Terminal.app** | Not supported | No graphics protocol support |

## Display Requirements

The terminal must report pixel dimensions via the `TIOCGWINSZ` ioctl. This is needed to:

- Compute cell pixel dimensions for image scaling
- Calculate tmux Unicode placeholder grid sizes
- Determine the correct render resolution

Most modern terminals report this correctly. If your terminal doesn't, images may be incorrectly sized.

## Tmux Requirements

mupager works inside tmux with these requirements:

- **tmux 3.3a or later** (for DCS passthrough support)
- **`allow-passthrough` enabled:**

```bash
# In tmux.conf:
set -g allow-passthrough on
```

Restart tmux after changing this setting. Without passthrough, Kitty graphics escapes are silently dropped by tmux and you'll see a blank screen.

In tmux, mupager uses Unicode placeholder characters for image positioning (see [[Kitty Graphics Protocol]]) and polls `#{window_active}` to hide/show images on tmux window switches.

## Neovim Requirements

For the Neovim plugin:

| Requirement | Version | Notes |
|-------------|---------|-------|
| **Neovim** | >= 0.9 | Required for RPC and `vim.uv` |
| **Telescope** | Optional | Used for TOC picker (`o` key) |

The `mupager` binary must be in `$PATH`, or configured via the `bin` setup option.

## Build Dependencies

For building from source, see [CONTRIBUTING.md](../CONTRIBUTING.md). The development environment is managed by Nix — `direnv allow` or `nix develop` provides all dependencies.

## Diagnostics

Run `mupager --diagnose` to check your environment. The output includes:

- **Terminal detection** — which terminal was detected (`TERM`, `TERM_PROGRAM`)
- **Kitty graphics support** — whether the protocol is expected to work
- **Tmux status** — whether running in tmux, passthrough status
- **Pixel dimensions** — terminal size in pixels and cells, computed cell pixel size
- **Color detection** — detected terminal foreground and background colors
- **Config file** — location and whether it was loaded successfully

Example:

```bash
$ mupager --diagnose
Terminal: kitty (TERM=xterm-kitty)
Graphics: Kitty protocol supported
Tmux: not detected
Display: 2560x1440 px, 213x56 cells (12x25 px/cell)
Colors: fg=#c0caf5, bg=#1a1b26
Config: /home/user/.config/mupager/config.toml (loaded)
```

Use this output when reporting issues or troubleshooting rendering problems.

## See Also

- [[Troubleshooting]] — common problems and solutions
- [[Kitty Graphics Protocol]] — protocol reference
