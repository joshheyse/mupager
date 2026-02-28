default:
    @just --list

# Configure the build directory
init:
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S . -B build
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

# Remove and recreate the build directory
clean:
    rm -rf build compile_commands.json
    just init

# Run all tests
test: build
    cd build && ctest -j$(nproc) --output-on-failure

# Format all C++ source files
fmt-cpp:
    find server -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Format all Lua source files
fmt-lua:
    stylua nvim/

# Format everything
fmt: fmt-cpp fmt-lua

# Run clang-tidy on all C++ translation units
lint-cpp: build
    find server -name '*.cpp' | xargs clang-tidy -p build \
        --extra-arg="-resource-dir=$$(clang -print-resource-dir)" \
        --header-filter="server/"

# Run selene on all Lua files
lint-lua:
    selene nvim/

# Lint everything
lint: lint-cpp lint-lua
