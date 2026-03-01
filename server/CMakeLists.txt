project(mupdf-server VERSION 0.1.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses++)

find_path(MSGPACK_INCLUDE_DIR NAMES msgpack.hpp REQUIRED)

# --- Server library (shared between exe and tests) ---

set(SERVER_LIB_SOURCES
  src/document.cpp
  src/pixmap.cpp
)

add_library(server-lib STATIC ${SERVER_LIB_SOURCES})

target_include_directories(server-lib
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC ${MUPDF_INCLUDE_DIRS}
  PRIVATE ${MSGPACK_INCLUDE_DIR}
)

target_link_libraries(server-lib
  PUBLIC PkgConfig::MUPDF
)

# --- Executable ---

add_executable(mupdf-server src/main.cpp)

set_target_properties(mupdf-server PROPERTIES
  VERSION ${PROJECT_VERSION}
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(mupdf-server
  PRIVATE ${MSGPACK_INCLUDE_DIR}
)

target_link_libraries(mupdf-server
  PRIVATE server-lib
  PRIVATE PkgConfig::NOTCURSES
  PRIVATE tomlplusplus::tomlplusplus
  PRIVATE CLI11::CLI11
  PRIVATE spdlog::spdlog
)

# --- Tests ---

set(SERVER_TEST_SOURCES
  src/document.test.cpp
  src/pixmap.test.cpp
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
