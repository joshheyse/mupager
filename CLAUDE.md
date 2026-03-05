# mupager

Terminal document viewer with Neovim integration. See PROJECT_DETAILS.md for full architecture.

## Project Structure

- `server/` — C++20 rendering server (MuPDF, Kitty graphics protocol, msgpack-rpc)
- `nvim/` — Neovim plugin (Lua)
- `cmake/` — CMake helpers (CPM.cmake, CompilerWarnings.cmake, Sanitizers.cmake)

## Build

```bash
just init [preset]   # configure + build (default: debug with ASan+UBSan)
just build [preset]  # incremental build
just test [preset]   # run tests
just build-tsan      # build with ThreadSanitizer
just test-tsan       # test with ThreadSanitizer
just sanitize        # run both ASan+UBSan and TSan test suites
just fmt             # format C++ and Lua
just lint            # lint C++ and Lua
just iwyu            # check includes with include-what-you-use
just clean           # remove build dirs and reinitialize
```

### Build Presets

| Preset | Sanitizers | Use |
|--------|-----------|-----|
| `debug` (default) | ASan + UBSan | Day-to-day development |
| `debug-tsan` | TSan | Concurrency testing |
| `debug-nosan` | None | Fast debug builds |
| `release` | None | Optimized builds |

Requires Nix flake dev shell (`direnv allow` or `nix develop`).

## Conventions

### C++

- C++20, clang, `-Werror`
- Style: `.clang-format` (LLVM-based, 160 col, 2-space indent)
- Naming: `.clang-tidy` — `CamelCase` types, `lower_case` functions/variables, `UPPER_CASE` constants, `trailing_` private members
- Tests: doctest, `*.test.cpp` files, discovered via `doctest_discover_tests()`
- All code must compile with `-Wall -Wextra -Wpedantic -Werror`
- Headers use `.hpp` extension; all project headers use `#pragma once`
- Facade/bundle headers re-export related sub-headers as a single entry point (e.g., a directory-level `.hpp` that `#include`s the sub-module headers)
- Includes: use include-what-you-use (`just iwyu`) — only include what you directly use
- Dependencies via CPM (doctest) and Nix (mupdf, msgpack-cxx)
- All classes and public APIs should have doxygen documentation
- Source file header comments are not needed
- Obvious single line comments are not needed, leave doxygen single line comments like those on members
- Section break comments are not needed

### Memory Safety Rules

- No raw `new`/`delete` — use `std::unique_ptr`, `std::make_unique`, or RAII wrappers
- Prefer `std::span` over raw pointer+size pairs
- Use `std::format` / `fmt::format` over `sprintf`/`snprintf`
- MuPDF C objects must be wrapped in RAII types that call `fz_drop_*` in destructors
- All `fz_try`/`fz_catch`/`fz_always` blocks must ensure cleanup on every path
- Be aware of setjmp/longjmp vs C++ RAII conflicts in MuPDF interop

### What's OK

- Virtual dispatch / vtables (needed for frontend abstraction)
- Limited template usage where it reduces duplication
- `std::string` by value (SSO handles common cases)
- Heap allocations for documents, pixmaps, and long-lived objects

### Pre-Edit Checklist

Before modifying C++ code, verify:
1. Read the file and surrounding context first
2. New code compiles with `-Wall -Wextra -Wpedantic -Werror`
3. No raw `new`/`delete` introduced
4. MuPDF resources have proper `fz_drop_*` cleanup
5. Test coverage exists for new logic (`*.test.cpp`)
6. Only include headers you directly use (IWYU principle)

### Lua

- Format: stylua (120 col, 2-space indent, no call parens)
- Lint: selene (neovim std)
- Neovim plugin code lives in `nvim/lua/mupager/`
