# Sanitizers.cmake — ASan+UBSan and TSan support

option(ENABLE_ASAN "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Mutual exclusivity check
if(ENABLE_ASAN AND ENABLE_TSAN)
  message(FATAL_ERROR "ASan and TSan cannot be enabled simultaneously")
endif()

add_library(sanitizer_options INTERFACE)

if(ENABLE_ASAN)
  message(STATUS "Sanitizers: ASan + UBSan")
  target_compile_options(sanitizer_options INTERFACE
    -fsanitize=address,undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
  )
  target_link_options(sanitizer_options INTERFACE
    -fsanitize=address,undefined
  )
elseif(ENABLE_TSAN)
  message(STATUS "Sanitizers: TSan")
  target_compile_options(sanitizer_options INTERFACE
    -fsanitize=thread
    -fno-omit-frame-pointer
  )
  target_link_options(sanitizer_options INTERFACE
    -fsanitize=thread
  )
else()
  message(STATUS "Sanitizers: none")
endif()

# Apply sanitizer options to a target.
function(target_set_sanitizers target)
  target_link_libraries(${target} PRIVATE sanitizer_options)
endfunction()
