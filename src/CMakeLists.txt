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

if(MUPAGER_STATIC_DEPS)
  target_link_libraries(server-lib
    PUBLIC PkgConfig::MUPDF_STATIC
    PUBLIC PkgConfig::NCURSESW_STATIC
    PUBLIC spdlog::spdlog
    PRIVATE CLI11::CLI11
    PRIVATE tomlplusplus::tomlplusplus
    PUBLIC msgpack-cxx
  )
else()
  target_link_libraries(server-lib
    PUBLIC PkgConfig::MUPDF
    PUBLIC PkgConfig::NCURSESW
    PUBLIC spdlog::spdlog
    PRIVATE CLI11::CLI11
    PRIVATE tomlplusplus::tomlplusplus
    PUBLIC msgpack-cxx
  )
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
