Review the current changes for code quality and safety issues. Launch 7 parallel sub-agents to analyze the codebase:

1. **Memory Safety** — Search for buffer overflows, use-after-free, dangling references, iterator invalidation, raw `new`/`delete`. Check that `std::span` is used over raw pointer+size pairs and `std::format` over `sprintf`.

2. **Resource Management** — Check for RAII violations, file descriptor leaks, and exception-path cleanup. Verify all resources are released in every control flow path including error paths.

3. **Correctness** — Look for off-by-one errors (especially 0-based vs 1-based page indexing), integer overflow in pixel coordinate calculations, and edge cases (empty documents, zero-size viewports, missing files).

4. **Code Quality** — Check for missing C++20 features that would simplify code, doxygen documentation gaps on public APIs, unnecessary copies (should use `const&` or `std::move`), and frontend/backend separation violations.

5. **Terminal & Escape Sequences** — Verify Kitty graphics protocol correctness (placement IDs, chunked transmission, Unicode placeholders), SGR escape sequence formatting, tmux pass-through wrapping, and OSC query response parsing.

6. **MuPDF Interop** — Check `fz_try`/`fz_catch`/`fz_always` correctness, verify `fz_drop_*` cleanup happens on all paths, flag setjmp/longjmp vs C++ RAII conflicts, and verify context threading safety.

7. **Include Hygiene (IWYU)** — Check that each `.cpp` and `.hpp` file only includes headers it directly uses. Flag transitive includes that should be made explicit, unnecessary includes that can be removed, and missing forward declarations that would reduce compile-time coupling. Follow the include-what-you-use principle: every symbol used must have its header directly included. Verify that all headers use `#pragma once`. Verify facade/bundle headers correctly re-export their sub-module headers.

For each issue found, report:
- **File and line number**
- **Severity**: critical / warning / suggestion
- **Description** of the issue
- **Fix** recommendation

Summarize findings grouped by category with critical issues first.
