# Common compile flags for C and C++
set(COMMON_C_FLAGS_LIST   Wall Wextra Wpedantic Werror=implicit-function-declaration)
set(COMMON_CXX_FLAGS_LIST Wall Wextra Wpedantic)

# Architecture-specific optimization
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(ARCH_FLAGS_LIST march=native mtune=native)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(ARCH_FLAGS_LIST march=armv8-a+simd)
else()
    set(ARCH_FLAGS_LIST "")
endif()

# Helper function to apply common flags to a target
function(aspira_set_c_flags TARGET)
    foreach(flag ${COMMON_C_FLAGS_LIST})
        target_compile_options(${TARGET} PRIVATE -${flag})
    endforeach()
    foreach(flag ${ARCH_FLAGS_LIST})
        target_compile_options(${TARGET} PRIVATE -${flag})
    endforeach()
endfunction()

function(aspira_set_cxx_flags TARGET)
    foreach(flag ${COMMON_CXX_FLAGS_LIST})
        target_compile_options(${TARGET} PRIVATE -${flag})
    endforeach()
    foreach(flag ${ARCH_FLAGS_LIST})
        target_compile_options(${TARGET} PRIVATE -${flag})
    endforeach()
endfunction()

# Release mode optimizations
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# Debug mode
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -DDEBUG")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=native -mtune=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native -mtune=native")
endif()

# Sanitizer support
if(ASPIRA_ENABLE_ASAN)
    set(SANITIZER_FLAGS "${SANITIZER_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
endif()
if(ASPIRA_ENABLE_TSAN)
    set(SANITIZER_FLAGS "${SANITIZER_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
endif()
if(ASPIRA_ENABLE_UBSAN)
    set(SANITIZER_FLAGS "${SANITIZER_FLAGS} -fsanitize=undefined -fno-omit-frame-pointer")
endif()

if(DEFINED SANITIZER_FLAGS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SANITIZER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${SANITIZER_FLAGS}")
endif()

# Link-time optimization in release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
