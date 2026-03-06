project(mupager VERSION 0.1.0 LANGUAGES CXX)

option(MUPAGER_STATIC_DEPS "Statically link dependencies (not glibc)" OFF)
if(MUPAGER_STATIC_DEPS)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  if(NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
  endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

if(MUPAGER_STATIC_DEPS)
  pkg_check_modules(MUPDF_STATIC REQUIRED IMPORTED_TARGET mupdf --static)
  pkg_check_modules(NCURSESW_STATIC REQUIRED IMPORTED_TARGET ncursesw --static)
endif()

find_path(MSGPACK_INCLUDE_DIR NAMES msgpack.hpp REQUIRED)

# --- Server library (shared between exe and tests) ---

set(SERVER_LIB_SOURCES
  src/args.cpp
  src/color.cpp
  src/config.cpp
  src/terminal/key_bindings.cpp
  src/document.cpp
  src/frontend.cpp
  src/diagnose.cpp
  src/app.cpp
  src/page.cpp
  src/page_manager.cpp
  src/converter.cpp
  src/graphics/kitty.cpp
  src/graphics/pixmap.cpp
  src/terminal/controller.cpp
  src/terminal/frontend.cpp
  src/terminal/input.cpp
  src/terminal/osc_query.cpp
  src/neovim/frontend.cpp
  src/neovim/rpc_transport.cpp
  src/util/base64.cpp
)

add_library(server-lib STATIC ${SERVER_LIB_SOURCES})

target_compile_definitions(server-lib PUBLIC
  MUPAGER_VERSION="${PROJECT_VERSION}"
  MUPAGER_GIT_HASH="${MUPAGER_GIT_HASH}"
  MUPAGER_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

target_include_directories(server-lib
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC ${MUPDF_INCLUDE_DIRS}
  PRIVATE ${MSGPACK_INCLUDE_DIR}
)

if(MUPAGER_STATIC_DEPS)
  target_link_libraries(server-lib
    PUBLIC PkgConfig::MUPDF_STATIC
    PUBLIC PkgConfig::NCURSESW_STATIC
    PUBLIC spdlog::spdlog
    PRIVATE CLI11::CLI11
    PRIVATE tomlplusplus::tomlplusplus
  )
else()
  target_link_libraries(server-lib
    PUBLIC PkgConfig::MUPDF
    PUBLIC PkgConfig::NCURSESW
    PUBLIC spdlog::spdlog
    PRIVATE CLI11::CLI11
    PRIVATE tomlplusplus::tomlplusplus
  )
endif()

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

install(TARGETS mupager RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/doc/mupager.1 DESTINATION share/man/man1)

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
