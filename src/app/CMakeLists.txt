file(GLOB APP_SOURCES src/*.cpp)

add_library(aspira_app STATIC ${APP_SOURCES})
target_include_directories(aspira_app PUBLIC include)
target_link_libraries(aspira_app PUBLIC aspira_core aspira_services Threads::Threads)
aspira_set_cxx_flags(aspira_app)
