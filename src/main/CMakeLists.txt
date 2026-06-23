add_executable(aspira_main main.cpp)
target_link_libraries(aspira_main PRIVATE
    aspira_core
    aspira_services
    aspira_app
    Threads::Threads
    ${RT_LIBRARIES}
    m)
aspira_set_cxx_flags(aspira_main)
