option(MUPAGER_STATIC_DEPS "Statically link dependencies (not glibc)" OFF)
if(MUPAGER_STATIC_DEPS)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  if(NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
  endif()
endif()

find_package(PkgConfig REQUIRED)

# mupdf: try pkg-config first, fall back to find_library (Homebrew has no .pc)
pkg_check_modules(MUPDF IMPORTED_TARGET mupdf)
if(NOT MUPDF_FOUND)
  find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h
    HINTS ENV CMAKE_PREFIX_PATH
    PATH_SUFFIXES include)
  find_library(MUPDF_LIBRARY mupdf
    HINTS ENV CMAKE_PREFIX_PATH
    PATH_SUFFIXES lib)
  find_library(MUPDF_THIRD_LIBRARY mupdf-third
    HINTS ENV CMAKE_PREFIX_PATH
    PATH_SUFFIXES lib)
  if(NOT MUPDF_INCLUDE_DIR OR NOT MUPDF_LIBRARY)
    message(FATAL_ERROR "mupdf not found via pkg-config or find_library")
  endif()
  add_library(PkgConfig::MUPDF INTERFACE IMPORTED)
  target_include_directories(PkgConfig::MUPDF INTERFACE ${MUPDF_INCLUDE_DIR})
  target_link_libraries(PkgConfig::MUPDF INTERFACE ${MUPDF_LIBRARY})
  if(MUPDF_THIRD_LIBRARY)
    target_link_libraries(PkgConfig::MUPDF INTERFACE ${MUPDF_THIRD_LIBRARY})
  endif()
  set(MUPDF_INCLUDE_DIRS ${MUPDF_INCLUDE_DIR})
endif()

pkg_check_modules(NCURSESW IMPORTED_TARGET ncursesw)
if(NOT NCURSESW_FOUND)
  find_package(Curses REQUIRED)
  add_library(PkgConfig::NCURSESW INTERFACE IMPORTED)
  target_include_directories(PkgConfig::NCURSESW INTERFACE ${CURSES_INCLUDE_DIRS})
  target_link_libraries(PkgConfig::NCURSESW INTERFACE ${CURSES_LIBRARIES})
endif()

if(MUPAGER_STATIC_DEPS AND NOT APPLE)
  pkg_check_modules(MUPDF_DEPS REQUIRED IMPORTED_TARGET
    freetype2 harfbuzz libjpeg libopenjp2 gumbo zlib)
endif()

# --- Server library (shared between exe and tests) ---

set(SERVER_LIB_SOURCES
  args.cpp
  color.cpp
  config.cpp
  terminal/key_bindings.cpp
  document.cpp
  frontend.cpp
  diagnose.cpp
  app.cpp
  page.cpp
  page_manager.cpp
  converter.cpp
  graphics/kitty.cpp
  graphics/pixmap.cpp
  terminal/controller.cpp
  terminal/frontend.cpp
  terminal/input.cpp
  terminal/osc_query.cpp
  neovim/frontend.cpp
  neovim/rpc_transport.cpp
  util/base64.cpp
)

add_library(server-lib STATIC ${SERVER_LIB_SOURCES})

target_compile_definitions(server-lib PUBLIC
  MUPAGER_VERSION="${PROJECT_VERSION}"
  MUPAGER_GIT_HASH="${MUPAGER_GIT_HASH}"
  MUPAGER_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

target_include_directories(server-lib
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${MUPDF_INCLUDE_DIRS}
)

target_link_libraries(server-lib
  PUBLIC PkgConfig::MUPDF
  PUBLIC PkgConfig::NCURSESW
  PUBLIC spdlog::spdlog
  PRIVATE CLI11::CLI11
  PRIVATE tomlplusplus::tomlplusplus
  PUBLIC msgpack-cxx
)

if(MUPAGER_STATIC_DEPS AND TARGET PkgConfig::MUPDF_DEPS)
  target_link_libraries(server-lib PUBLIC PkgConfig::MUPDF_DEPS)
endif()

target_set_warnings(server-lib)
target_set_sanitizers(server-lib)

# --- Executable ---

add_executable(mupager main.cpp)

set_target_properties(mupager PROPERTIES
  VERSION ${PROJECT_VERSION}
  POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(mupager
  PRIVATE server-lib
)

install(TARGETS mupager RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/doc/mupager.1 DESTINATION share/man/man1)

# --- Tests ---

set(SERVER_TEST_SOURCES
  geometry.test.cpp
  document.test.cpp
  graphics/pixmap.test.cpp
  util/base64.test.cpp
  graphics/kitty.test.cpp
  color.test.cpp
  terminal/key_bindings.test.cpp
  converter.test.cpp
)

add_executable(ServerTests ${SERVER_TEST_SOURCES})

target_compile_definitions(ServerTests
  PRIVATE PROJECT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
)

target_link_libraries(ServerTests
  PRIVATE server-lib
  PRIVATE doctest::doctest_with_main
)

get_target_property(_doctest_inc doctest::doctest_with_main INTERFACE_INCLUDE_DIRECTORIES)
if(_doctest_inc)
  target_include_directories(ServerTests SYSTEM PRIVATE ${_doctest_inc})
endif()

target_set_warnings(ServerTests)
target_set_sanitizers(ServerTests)

doctest_discover_tests(ServerTests)
