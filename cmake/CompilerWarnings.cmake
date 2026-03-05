# CompilerWarnings.cmake — INTERFACE library + helper function for compiler warnings

add_library(compiler_warnings INTERFACE)

target_compile_options(compiler_warnings INTERFACE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wno-error=unused-parameter
  "-Wno-error=#warnings"
)

# Apply compiler warnings to a target via INTERFACE linkage.
function(target_set_warnings target)
  target_link_libraries(${target} PRIVATE compiler_warnings)
endfunction()
