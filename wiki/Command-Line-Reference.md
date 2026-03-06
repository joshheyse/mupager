# Command Line Reference

## Synopsis

```
mupager [OPTIONS] <file>
```

## Options

### Information

| Flag | Description |
|------|-------------|
| `--version` | Print version and exit |
| `--diagnose` | Print diagnostic information (terminal, display, tmux, config) and exit |

### Display

| Flag | Values | Default | Description |
|------|--------|---------|-------------|
| `--view-mode MODE` | `continuous`, `page`, `page-height`, `side-by-side` | `continuous` | View mode |
| `--theme THEME` | `dark`, `light`, `auto`, `terminal` | `dark` | Color theme |
| `--render-scale SCALE` | `auto`, `never`, `0.25`, `0.5`, `1`, `2`, `4` | `auto` | Render scale for cached page rendering |
| `--scroll-lines N` | integer | `3` | Lines per scroll step |
| `--terminal-fg COLOR` | `#RRGGBB` | (auto-detect) | Override terminal foreground color |
| `--terminal-bg COLOR` | `#RRGGBB` | (auto-detect) | Override terminal background color |
| `--show-stats` | flag | off | Show cache statistics in the statusline |

### Frontend

| Flag | Values | Default | Description |
|------|--------|---------|-------------|
| `--mode MODE` | `terminal`, `neovim` | `terminal` | Frontend mode (normally set automatically) |

### File Handling

| Flag | Description |
|------|-------------|
| `--watch` | Auto-reload document when the file changes on disk |
| `--converter CMD` | Shell command to convert the file to PDF (overrides config converters) |
| `--converter-pattern PAT=CMD` | Add a glob pattern → converter mapping (repeatable) |

### System

| Flag | Default | Description |
|------|---------|-------------|
| `-c PATH`, `--config PATH` | `$XDG_CONFIG_HOME/mupager/config.toml` | Config file path |
| `--max-page-cache MB` | `64` | Maximum page cache size in megabytes |
| `--log-level LEVEL` | `info` | Log level: `trace`, `debug`, `info`, `warn`, `error`, `critical` |
| `--log-file PATH` | `$XDG_STATE_HOME/mupager/mupager.log` | Log file path |

## Environment Variables

| Variable | Description |
|----------|-------------|
| `XDG_CONFIG_HOME` | Base directory for config files (default: `~/.config`) |
| `XDG_STATE_HOME` | Base directory for state/log files (default: `~/.local/state`) |
| `SPDLOG_LEVEL` | Fallback log level when `--log-level` is not specified |
| `NVIM` | Neovim RPC socket path — set automatically inside Neovim jobs |
| `TERM` | Terminal type identifier |
| `TERM_PROGRAM` | Terminal emulator name (e.g., `kitty`, `WezTerm`) |
| `TMUX` | Tmux socket path — enables tmux-aware visibility management |

## Precedence

Settings are resolved in this order (highest priority first):

1. **CLI flags** — always win
2. **config.toml** — applied for settings not set on the CLI
3. **Built-in defaults**

In Neovim mode, `require("mupager").setup()` options are passed as CLI flags, so they follow the same precedence.

## Examples

View a PDF:

```bash
mupager document.pdf
```

Open in page-height mode with light theme:

```bash
mupager --view-mode page-height --theme light paper.pdf
```

Use a custom config file:

```bash
mupager --config ~/my-config.toml thesis.pdf
```

Convert and watch a Markdown file:

```bash
mupager --converter "pandoc %i -o %o" --watch notes.md
```

Debug logging:

```bash
mupager --log-level debug novel.epub
```

Run diagnostics:

```bash
mupager --diagnose
```

## Exit Status

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | Error (invalid arguments, missing file, render failure, config error) |
