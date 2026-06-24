# Minimal C example
add_executable(simple_acquisition simple_acquisition.c)
target_link_libraries(simple_acquisition PRIVATE aspira_core Threads::Threads ${RT_LIBRARIES} m)
aspira_set_c_flags(simple_acquisition)

# OpenCV visual test
if(OpenCV_FOUND)
    add_executable(visual_test visual_test.cpp)
    target_link_libraries(visual_test PRIVATE aspira_core aspira_app Threads::Threads ${RT_LIBRARIES} ${OpenCV_LIBS} m)
    target_include_directories(visual_test PRIVATE ${OpenCV_INCLUDE_DIRS})
    aspira_set_cxx_flags(visual_test)
endif()

# Full C++ demo
add_executable(full_system full_system.cpp)
target_link_libraries(full_system PRIVATE aspira_core aspira_services aspira_app Threads::Threads ${RT_LIBRARIES} m)
aspira_set_cxx_flags(full_system)
