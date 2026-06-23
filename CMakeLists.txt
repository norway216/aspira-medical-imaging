cmake_minimum_required(VERSION 3.19)
project(aspira VERSION 0.1.0 LANGUAGES C CXX
        DESCRIPTION "High-performance medical imaging framework")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(ASPIRA_BUILD_TESTS "Build tests" ON)
option(ASPIRA_BUILD_EXAMPLES "Build examples" ON)
option(ASPIRA_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ASPIRA_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ASPIRA_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# POSIX feature test macros for clock_gettime, shm_open, etc.
add_compile_definitions(_POSIX_C_SOURCE=199309L _GNU_SOURCE)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CompilerOptions.cmake)
include(cmake/FindDependencies.cmake)

add_subdirectory(src/core)
add_subdirectory(src/services)
add_subdirectory(src/app)
add_subdirectory(src/main)

if(ASPIRA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ASPIRA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
