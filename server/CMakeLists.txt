project(mupdf-server VERSION 0.1.0 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)
pkg_check_modules(NOTCURSES REQUIRED IMPORTED_TARGET notcurses++)

find_path(MSGPACK_INCLUDE_DIR NAMES msgpack.hpp REQUIRED)

# --- Server library (shared between exe and tests) ---

set(SERVER_SOURCES
  src/main.cpp
)

set(SERVER_HEADERS
)

# --- Executable ---

add_executable(mupdf-server ${SERVER_SOURCES})

set_target_properties(mupdf-server PROPERTIES
  VERSION ${PROJECT_VERSION}
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(mupdf-server
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
  PRIVATE ${MSGPACK_INCLUDE_DIR}
)

target_link_libraries(mupdf-server
  PRIVATE PkgConfig::MUPDF
  PRIVATE PkgConfig::NOTCURSES
  PRIVATE tomlplusplus::tomlplusplus
  PRIVATE CLI11::CLI11
  PRIVATE spdlog::spdlog
)

# --- Tests ---

# set(SERVER_TEST_SOURCES
# )

# add_executable(ServerTests ${SERVER_TEST_SOURCES})
# target_include_directories(ServerTests
#   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
#   PRIVATE ${MSGPACK_INCLUDE_DIR}
# )
# target_link_libraries(ServerTests
#   PRIVATE PkgConfig::MUPDF
#   PRIVATE doctest::doctest_with_main
# )
# doctest_discover_tests(ServerTests)
