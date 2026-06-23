# Find pthreads (required)
find_package(Threads REQUIRED)

# Find librt for shm_open, mq_open (required)
find_library(RT_LIBRARY rt)
if(RT_LIBRARY)
    set(RT_LIBRARIES ${RT_LIBRARY})
else()
    set(RT_LIBRARIES "")
endif()

# Check for stdatomic.h
include(CheckIncludeFile)
check_include_file(stdatomic.h HAVE_STDATOMIC_H)
if(NOT HAVE_STDATOMIC_H)
    message(FATAL_ERROR "stdatomic.h is required for C11 atomics")
endif()

# libatomic on some platforms
find_library(ATOMIC_LIBRARY atomic)
if(ATOMIC_LIBRARY)
    set(ATOMIC_LIBRARIES ${ATOMIC_LIBRARY})
else()
    set(ATOMIC_LIBRARIES "")
endif()

# libnuma for NUMA-aware allocation (optional)
find_package(PkgConfig QUIET)
find_library(NUMA_LIBRARY numa)
if(NUMA_LIBRARY)
    set(NUMA_FOUND TRUE)
    set(NUMA_LIBRARIES ${NUMA_LIBRARY})
    message(STATUS "Found libnuma: ${NUMA_LIBRARY}")
else()
    set(NUMA_FOUND FALSE)
    set(NUMA_LIBRARIES "")
    message(STATUS "libnuma not found - NUMA-aware allocation disabled")
endif()

# Catch2 for testing
if(ASPIRA_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED)
endif()

# Boost (optional components for services)
find_package(Boost QUIET)
if(Boost_FOUND)
    message(STATUS "Found Boost ${Boost_VERSION}")
endif()
