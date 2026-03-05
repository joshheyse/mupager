project(mupager VERSION 0.1.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

find_path(MSGPACK_INCLUDE_DIR NAMES msgpack.hpp REQUIRED)

# --- Server library (shared between exe and tests) ---

set(SERVER_LIB_SOURCES
  src/args.cpp
  src/color.cpp
  src/config.cpp
  src/terminal/key_bindings.cpp
  src/document.cpp
  src/frontend.cpp
  src/app.cpp
  src/converter.cpp
  src/graphics/kitty.cpp
  src/graphics/pixmap.cpp
  src/terminal/controller.cpp
  src/terminal/frontend.cpp
  src/terminal/input.cpp
  src/terminal/loop.cpp
  src/terminal/osc_query.cpp
  src/neovim/frontend.cpp
  src/neovim/loop.cpp
  src/neovim/rpc_transport.cpp
  src/util/base64.cpp
)

add_library(server-lib STATIC ${SERVER_LIB_SOURCES})

target_include_directories(server-lib
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC ${MUPDF_INCLUDE_DIRS}
  PRIVATE ${MSGPACK_INCLUDE_DIR}
)

target_link_libraries(server-lib
  PUBLIC PkgConfig::MUPDF
  PUBLIC PkgConfig::NCURSESW
  PUBLIC spdlog::spdlog
  PRIVATE CLI11::CLI11
  PRIVATE tomlplusplus::tomlplusplus
)

target_set_warnings(server-lib)
target_set_sanitizers(server-lib)

# --- Executable ---

add_executable(mupager src/main.cpp)

set_target_properties(mupager PROPERTIES
  VERSION ${PROJECT_VERSION}
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(mupager
  PRIVATE ${MSGPACK_INCLUDE_DIR}
)

target_link_libraries(mupager
  PRIVATE server-lib
)

# --- Tests ---

set(SERVER_TEST_SOURCES
  src/geometry.test.cpp
  src/document.test.cpp
  src/graphics/pixmap.test.cpp
  src/util/base64.test.cpp
  src/graphics/kitty.test.cpp
  src/color.test.cpp
  src/terminal/key_bindings.test.cpp
  src/converter.test.cpp
)

add_executable(ServerTests ${SERVER_TEST_SOURCES})

target_compile_definitions(ServerTests
  PRIVATE PROJECT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

target_include_directories(ServerTests
  PRIVATE ${MSGPACK_INCLUDE_DIR}
)

target_link_libraries(ServerTests
  PRIVATE server-lib
  PRIVATE doctest::doctest_with_main
)

target_set_warnings(ServerTests)
target_set_sanitizers(ServerTests)

doctest_discover_tests(ServerTests)
