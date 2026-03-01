# mupdf.nvim

Terminal document viewer with Neovim integration. See PROJECT_DETAILS.md for full architecture.

## Project Structure

- `server/` — C++17 rendering server (MuPDF, Kitty graphics protocol, msgpack-rpc)
- `nvim/` — Neovim plugin (Lua)
- `cmake/` — CMake helpers (CPM.cmake)

## Build

```bash
just init    # configure + build
just build   # incremental build
just test    # run all tests
just fmt     # format C++ and Lua
just lint    # lint C++ and Lua
```

Requires Nix flake dev shell (`direnv allow` or `nix develop`).

## Conventions

### C++

- C++17, clang, `-Werror`
- Style: `.clang-format` (LLVM-based, 160 col, 2-space indent)
- Naming: `.clang-tidy` — `CamelCase` types, `lower_case` functions/variables, `UPPER_CASE` constants, `trailing_` private members
- Tests: doctest, `*.test.cpp` files, discovered via `doctest_discover_tests()`
- All code must compile with `-Wall -Wextra -Wpedantic -Werror`
- Test files follow pattern `*.test.cpp` using doctest framework
- Dependencies via CPM (doctest) and Nix (mupdf, msgpack-cxx)
- All classes and public APIs should have doxygen documentation
- Source File header comments are not needed
- Obvious single line comments are not needed
- Section break comments are not needed

### Lua

- Format: stylua (120 col, 2-space indent, no call parens)
- Lint: selene (neovim std)
- Neovim plugin code lives in `nvim/lua/mupdf/`
