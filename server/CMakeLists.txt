project(mupager VERSION 0.1.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

find_path(MSGPACK_INCLUDE_DIR NAMES msgpack.hpp REQUIRED)

# --- Server library (shared between exe and tests) ---

set(SERVER_LIB_SOURCES
  src/args.cpp
  src/document.cpp
  src/pixmap.cpp
  src/base64.cpp
  src/kitty.cpp
  src/terminal_frontend.cpp
  src/app.cpp
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
)

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
  PRIVATE tomlplusplus::tomlplusplus
)

# --- Tests ---

set(SERVER_TEST_SOURCES
  src/document.test.cpp
  src/pixmap.test.cpp
  src/base64.test.cpp
  src/kitty.test.cpp
)

add_executable(ServerTests ${SERVER_TEST_SOURCES})

target_compile_definitions(ServerTests
  PRIVATE PROJECT_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test"
)

target_include_directories(ServerTests
  PRIVATE ${MSGPACK_INCLUDE_DIR}
)

target_link_libraries(ServerTests
  PRIVATE server-lib
  PRIVATE doctest::doctest_with_main
)

doctest_discover_tests(ServerTests)
