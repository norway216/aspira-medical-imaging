# Minimal C example
add_executable(simple_acquisition simple_acquisition.c)
target_link_libraries(simple_acquisition PRIVATE aspira_core Threads::Threads ${RT_LIBRARIES} m)
aspira_set_c_flags(simple_acquisition)

# Full C++ demo
add_executable(full_system full_system.cpp)
target_link_libraries(full_system PRIVATE aspira_core aspira_services aspira_app Threads::Threads ${RT_LIBRARIES} m)
aspira_set_cxx_flags(full_system)
