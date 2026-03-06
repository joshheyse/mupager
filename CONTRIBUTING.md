# Contributing to mupager

## Prerequisites

mupager uses [Nix](https://nixos.org/) for reproducible builds. Install Nix and enable flakes, then:

```bash
git clone https://github.com/joshheyse/mupager.git
cd mupager
direnv allow   # or: nix develop
```

This drops you into a shell with all compilers, libraries, and tools pre-configured.

## Quick Start

```bash
just init       # configure (debug + ASan/UBSan) and build
just test       # run tests
just fmt        # format C++ and Lua
just lint       # lint C++ and Lua
mupager doc.pdf # run from build/debug/src/mupager
```

## Build Presets

| Preset | Sanitizers | Use |
|--------|-----------|-----|
| `debug` (default) | ASan + UBSan | Day-to-day development |
| `debug-tsan` | TSan | Concurrency testing |
| `debug-nosan` | None | Fast debug builds |
| `release` | None | Optimized builds |

Select a preset with `just init <preset>` or `just build <preset>`.

## Common Commands

| Command | Description |
|---------|-------------|
| `just init [preset]` | Configure + build (default: `debug`) |
| `just build [preset]` | Incremental build |
| `just test [preset]` | Run tests |
| `just build-tsan` | Build with ThreadSanitizer |
| `just test-tsan` | Test with ThreadSanitizer |
| `just sanitize` | Run both ASan+UBSan and TSan test suites |
| `just fmt` | Format C++ (clang-format) and Lua (stylua) |
| `just lint` | Lint C++ (clang-tidy) and Lua (selene) |
| `just iwyu` | Check includes with include-what-you-use |
| `just clean` | Remove build dirs and reinitialize |

## Testing

Tests use [doctest](https://github.com/doctest/doctest) and live alongside source files as `*.test.cpp`. CMake discovers them automatically via `doctest_discover_tests()`.

```bash
just test          # ASan + UBSan tests
just test-tsan     # ThreadSanitizer tests
just sanitize      # run both suites
```

## Code Style

### C++

- **Standard:** C++20, compiled with clang and `-Wall -Wextra -Wpedantic -Werror`
- **Formatter:** clang-format (LLVM-based, 160 column limit, 2-space indent) — see `.clang-format`
- **Linter:** clang-tidy — see `.clang-tidy`
- **Naming conventions:**
  - `CamelCase` for types and classes
  - `lower_case` for functions and variables
  - `UPPER_CASE` for constants
  - `trailing_` suffix for private members
- **Headers:** use `.hpp` extension with `#pragma once`
- **Includes:** follow include-what-you-use — only include headers you directly use (`just iwyu` to check)
- **Documentation:** Doxygen comments on all classes and public APIs
- **Memory safety:**
  - No raw `new`/`delete` — use `std::unique_ptr`, `std::make_unique`, or RAII wrappers
  - Prefer `std::span` over raw pointer+size pairs
  - Use `std::format` over `sprintf`/`snprintf`
  - MuPDF C objects must be wrapped in RAII types that call `fz_drop_*` in destructors

### Lua

- **Formatter:** [stylua](https://github.com/JohnnyMorganz/StyLua) (120 column limit, 2-space indent, no call parens)
- **Linter:** [selene](https://github.com/Kampfkarren/selene) (neovim standard library)
- Neovim plugin code lives in `nvim/lua/mupager/`

### Formatting and Linting

```bash
just fmt    # auto-format all C++ and Lua files
just lint   # run clang-tidy and selene
```

Both are enforced in CI. Run them before committing.

## Project Structure

```
src/              C++20 rendering server (source files, headers, CMakeLists.txt)
nvim/             Neovim plugin (submodule → mupager.nvim)
  lua/mupager/    Plugin Lua modules
  doc/            Vimdoc help files
cmake/            CMake helpers (CPM, warnings, sanitizers)
fixtures/         Example config files
doc/              Man page
```

## Dependencies

| Library | Purpose | Source |
|---------|---------|--------|
| [MuPDF](https://mupdf.com/) | Document rendering | Nix |
| [msgpack-cxx](https://github.com/msgpack/msgpack-c) | Neovim RPC protocol | CPM |
| [ncurses](https://invisible-island.net/ncurses/) | Terminal handling | Nix |
| [toml++](https://github.com/marzer/tomlplusplus) | Config file parsing | CPM |
| [CLI11](https://github.com/CLIUtils/CLI11) | Argument parsing | CPM |
| [spdlog](https://github.com/gabime/spdlog) | Logging | CPM |
| [doctest](https://github.com/doctest/doctest) | Testing | CPM |

## Pull Requests

1. Fork the repository and create a feature branch
2. Make your changes, keeping commits focused
3. Ensure `just fmt` and `just lint` pass with no warnings
4. Ensure `just sanitize` passes (both ASan+UBSan and TSan)
5. Add tests for new functionality in `*.test.cpp` files
6. Open a pull request with a clear description of the change
