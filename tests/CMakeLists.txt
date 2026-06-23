# Main test executable
add_executable(aspira_tests
    core/test_ring_buffer.cpp
    core/test_memory_pool.cpp
    core/test_thread_pool.cpp
    core/test_signal_pipeline.cpp
    services/test_security_manager.cpp
    services/test_workflow_orchestrator.cpp
    integration/test_full_pipeline.cpp
)

target_link_libraries(aspira_tests PRIVATE
    aspira_core
    aspira_services
    aspira_app
    Catch2::Catch2WithMain
    Threads::Threads
    ${RT_LIBRARIES}
    m)

aspira_set_cxx_flags(aspira_tests)

# Register with CTest
include(CTest)
add_test(NAME AspiraTests COMMAND aspira_tests)
