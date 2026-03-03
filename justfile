default:
    @just --list

# Configure the build directory
init:
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="${CC:-cc}" -DCMAKE_CXX_COMPILER="${CXX:-c++}" -S . -B build
    ln -sf build/compile_commands.json compile_commands.json
    cmake --build build

# Ensure build directory exists
[private]
ensure-init:
    #!/usr/bin/env bash
    if [ ! -d build ]; then
      just init
    fi

# Build the project
build: ensure-init
    cmake --build build

# Run with test PDF (pass extra args after --)
run *ARGS: build
    SPDLOG_LEVEL=debug ./build/server/mupager fixtures/test.pdf {{ARGS}}

# Remove and recreate the build directory
clean:
    rm -rf build compile_commands.json
    just init

# Run all tests
test: build
    cd build && ctest -j$(nproc) --output-on-failure

# Format all C++ source files
fmt-cpp:
    find server -name '*.cpp' -o -name '*.h' -o -name '*.hpp' | xargs clang-format -i

# Format all Lua source files
fmt-lua:
    stylua nvim/

# Format everything
fmt: fmt-cpp fmt-lua

# Run clang-tidy on all C++ translation units
lint-cpp: build
    #!/usr/bin/env bash
    set -euo pipefail
    extra_args=()
    while IFS= read -r dir; do
        extra_args+=(--extra-arg=-isystem --extra-arg="$dir")
    done < <(clang++ -E -x c++ /dev/null -v 2>&1 \
        | sed -n '/#include <...> search starts here:/,/End of search list/p' \
        | grep '^ ' | sed 's/^ *//')
    find server -name '*.cpp' | xargs clang-tidy -p build \
        "${extra_args[@]}" \
        --header-filter="server/"

# Run selene on all Lua files
lint-lua:
    selene nvim/

# Lint everything
lint: lint-cpp lint-lua
