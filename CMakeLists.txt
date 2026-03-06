cmake_minimum_required(VERSION 3.26)

execute_process(COMMAND git describe --tags --match "v*" --abbrev=0
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE MUPAGER_GIT_TAG OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(MUPAGER_GIT_TAG MATCHES "^v(.+)$")
  set(MUPAGER_VERSION_FROM_GIT "${CMAKE_MATCH_1}")
else()
  set(MUPAGER_VERSION_FROM_GIT "0.0.0")
endif()

project(mupager VERSION ${MUPAGER_VERSION_FROM_GIT} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BUILD_SHARED_LIBS OFF)

# Debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-ggdb -fno-omit-frame-pointer)
endif()

execute_process(COMMAND git describe --always --dirty
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE MUPAGER_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT MUPAGER_GIT_HASH)
  set(MUPAGER_GIT_HASH "unknown")
endif()

include(cmake/CPM.cmake)
include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

CPMAddPackage(
  NAME doctest
  GITHUB_REPOSITORY doctest/doctest
  GIT_TAG v2.4.12
  SYSTEM TRUE
)
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

CPMAddPackage(
  NAME tomlplusplus
  GITHUB_REPOSITORY marzer/tomlplusplus
  GIT_TAG v3.4.0
  SYSTEM TRUE
)

CPMAddPackage(
  NAME CLI11
  GITHUB_REPOSITORY CLIUtils/CLI11
  GIT_TAG v2.4.2
  SYSTEM TRUE
)

CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG v1.15.0
  SYSTEM TRUE
  OPTIONS "SPDLOG_USE_STD_FORMAT ON"
)

CPMAddPackage(
  NAME msgpack-cxx
  GITHUB_REPOSITORY msgpack/msgpack-c
  GIT_TAG cpp-6.1.1
  DOWNLOAD_ONLY TRUE
)
if(msgpack-cxx_ADDED)
  add_library(msgpack-cxx INTERFACE)
  target_include_directories(msgpack-cxx SYSTEM INTERFACE ${msgpack-cxx_SOURCE_DIR}/include)
endif()

enable_testing()

add_subdirectory(src)

