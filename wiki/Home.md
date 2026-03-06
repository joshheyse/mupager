# mupager

Terminal document viewer with pixel-perfect rendering via the Kitty graphics protocol. Supports PDF, EPUB, XPS, CBZ/CBR, FB2, MOBI, SVG, HTML, and images through MuPDF, with both a standalone TUI and a Neovim plugin backend.

## User Guide

- [[Features]] — Supported formats, view modes, search, TOC, link hints, visual selection, themes
- [[Getting Started]] — Installation, first run, diagnostics
- [[Command Line Reference]] — CLI flags, environment variables, examples
- [[Configuration]] — Full config.toml reference with colors, keys, and converters
- [[Terminal Mode]] — Standalone TUI keybindings, overlays, command mode
- [[Neovim Mode]] — Plugin setup, commands, Telescope integration
- [[Converters and Watch Mode]] — File conversion and live-reload
- [[Requirements and Compatibility]] — Terminal, tmux, and Neovim requirements

## Internals

- [[Render Pipeline]] — How documents go from file to screen
- [[Kitty Graphics Protocol]] — Escape sequences, chunking, tmux placeholders

## Help

- [[Known Issues and Gotchas]] — Platform quirks and workarounds
- [[Troubleshooting]] — Common problems and solutions
- [CONTRIBUTING.md](../CONTRIBUTING.md) — Build, test, and code style guide for developers
