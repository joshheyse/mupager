# Getting Started

## Installation

### Nix

```bash
# Run directly
nix run github:joshheyse/mupager -- document.pdf

# Install to profile
nix profile install github:joshheyse/mupager
```

### Pre-built Binaries

Download from the [Releases](https://github.com/joshheyse/mupager/releases) page.

### From Source

```bash
git clone https://github.com/joshheyse/mupager.git
cd mupager
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build
# Binary at build/server/mupager
```

See [CONTRIBUTING.md](../CONTRIBUTING.md) for development builds with sanitizers.

## First Run

### Terminal Mode

```bash
mupager document.pdf
```

Press `?` to open the help overlay with all keybindings. Press `q` to quit.

### Neovim Mode

Add the plugin with [lazy.nvim](https://github.com/folke/lazy.nvim):

```lua
{
  "joshheyse/mupager",
  ft = { "pdf" },
  config = function()
    require("mupager").setup()
  end,
}
```

Then open any PDF in Neovim — the plugin intercepts the buffer and renders the document in-place.

## Diagnostics

Run `mupager --diagnose` to check your environment:

```bash
mupager --diagnose
```

This prints information about:

- Terminal detection (Kitty, WezTerm, Ghostty)
- Kitty graphics protocol support
- Tmux status and passthrough configuration
- Terminal pixel dimensions (TIOCGWINSZ)
- Detected terminal foreground/background colors
- Config file location and contents

Use this output when reporting issues.

## Next Steps

- [[Features]] — full feature overview
- [[Command Line Reference]] — all CLI options
- [[Configuration]] — customize themes, keybindings, and colors
- [[Terminal Mode]] or [[Neovim Mode]] — mode-specific details
