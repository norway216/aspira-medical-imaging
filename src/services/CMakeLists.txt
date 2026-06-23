file(GLOB SERVICES_SOURCES src/*.cpp)

add_library(aspira_services STATIC ${SERVICES_SOURCES})
target_include_directories(aspira_services PUBLIC include)
target_link_libraries(aspira_services PUBLIC aspira_core Threads::Threads)
aspira_set_cxx_flags(aspira_services)

if(Boost_FOUND)
    target_link_libraries(aspira_services PUBLIC Boost::boost)
    target_compile_definitions(aspira_services PRIVATE ASPIRA_HAS_BOOST)
endif()
