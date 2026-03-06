default_preset := "debug"

default:
    @just --list

# Configure a build preset (default: debug with ASan+UBSan)
init preset=default_preset:
    cmake --preset {{preset}}
    ln -sf build/{{preset}}/compile_commands.json compile_commands.json
    cmake --build build/{{preset}}

# Ensure build directory exists for a preset
[private]
ensure-init preset=default_preset:
    #!/usr/bin/env bash
    if [ ! -d build/{{preset}} ]; then
      just init {{preset}}
    fi

# Build a preset (default: debug)
build preset=default_preset: (ensure-init preset)
    cmake --build build/{{preset}}

# Build with ThreadSanitizer
build-tsan: (build "debug-tsan")

# Run with test PDF (pass extra args after --)
run *ARGS: build
    ./build/debug/src/mupager --config fixtures/mupager.toml fixtures/test.pdf {{ARGS}}

# Remove build directories and reinitialize
clean:
    rm -rf build compile_commands.json
    just init

# Run tests for a preset (default: debug)
test preset=default_preset: (build preset)
    cd build/{{preset}} && ctest -j$(nproc) --output-on-failure

# Run tests with ThreadSanitizer
test-tsan: (test "debug-tsan")

# Run both ASan+UBSan and TSan test suites
sanitize: test test-tsan

# Format all C++ source files
fmt-cpp:
    find src -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Format all Lua source files
fmt-lua:
    stylua nvim/

# Format everything
fmt: fmt-cpp fmt-lua

# Run clang-tidy on all C++ translation units (excludes test files)
lint-cpp preset=default_preset: (build preset)
    #!/usr/bin/env bash
    set -euo pipefail
    extra_args=()
    while IFS= read -r dir; do
        extra_args+=(--extra-arg=-isystem --extra-arg="$dir")
    done < <(clang++ -E -x c++ /dev/null -v 2>&1 \
        | sed -n '/#include <...> search starts here:/,/End of search list/p' \
        | grep '^ ' | sed 's/^ *//')
    find src -name '*.cpp' ! -name '*.test.cpp' | xargs clang-tidy -p build/{{preset}} \
        "${extra_args[@]}" \
        --header-filter="src/"

# Run clang-tidy --fix on all C++ translation units (excludes test files)
lint-fix preset=default_preset: (build preset)
    #!/usr/bin/env bash
    set -euo pipefail
    extra_args=()
    while IFS= read -r dir; do
        extra_args+=(--extra-arg=-isystem --extra-arg="$dir")
    done < <(clang++ -E -x c++ /dev/null -v 2>&1 \
        | sed -n '/#include <...> search starts here:/,/End of search list/p' \
        | grep '^ ' | sed 's/^ *//')
    find src -name '*.cpp' ! -name '*.test.cpp' | xargs clang-tidy -p build/{{preset}} \
        "${extra_args[@]}" \
        --header-filter="src/" \
        --fix

# Run include-what-you-use on all C++ source files (excludes test files)
iwyu preset=default_preset: (build preset)
    #!/usr/bin/env bash
    set -euo pipefail
    find src -name '*.cpp' ! -name '*.test.cpp' | while read -r f; do
      echo "=== $f ==="
      include-what-you-use -p build/{{preset}} "$f" 2>&1 || true
    done

# Run selene on all Lua files
lint-lua:
    selene nvim/

# Lint everything
lint: lint-cpp lint-lua

# Check C++ formatting (no changes)
fmt-check-cpp:
    find src -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror

# Check Lua formatting (no changes)
fmt-check-lua:
    stylua --check nvim/

# Check all formatting
fmt-check: fmt-check-cpp fmt-check-lua
