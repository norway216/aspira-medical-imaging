file(GLOB CORE_SOURCES src/*.c)

add_library(aspira_core STATIC ${CORE_SOURCES})
target_include_directories(aspira_core PUBLIC include)
aspira_set_c_flags(aspira_core)
target_link_libraries(aspira_core PUBLIC
    Threads::Threads
    ${RT_LIBRARIES}
    ${ATOMIC_LIBRARIES})

if(NUMA_FOUND)
    target_compile_definitions(aspira_core PRIVATE ASPIRA_HAS_NUMA)
    target_link_libraries(aspira_core PUBLIC ${NUMA_LIBRARIES})
endif()

# Also build as shared library for dynamic loading
add_library(aspira_core_shared SHARED ${CORE_SOURCES})
target_include_directories(aspira_core_shared PUBLIC include)
aspira_set_c_flags(aspira_core_shared)
target_link_libraries(aspira_core_shared PUBLIC
    Threads::Threads
    ${RT_LIBRARIES}
    ${ATOMIC_LIBRARIES})
if(NUMA_FOUND)
    target_compile_definitions(aspira_core_shared PRIVATE ASPIRA_HAS_NUMA)
    target_link_libraries(aspira_core_shared PUBLIC ${NUMA_LIBRARIES})
endif()
set_target_properties(aspira_core_shared PROPERTIES OUTPUT_NAME aspira_core)
