cmake_minimum_required(VERSION 3.26)
project(mupager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror -ggdb -Wno-error=unused-parameter -Wno-error=#warnings")

set(BUILD_SHARED_LIBS OFF)

include(cmake/CPM.cmake)

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
)

enable_testing()

add_subdirectory(server)
