file(GLOB APP_SOURCES src/*.cpp)

add_library(aspira_app STATIC ${APP_SOURCES})
target_include_directories(aspira_app PUBLIC include)
target_link_libraries(aspira_app PUBLIC aspira_core aspira_services Threads::Threads)
aspira_set_cxx_flags(aspira_app)

if(OpenCV_FOUND)
    target_compile_definitions(aspira_app PUBLIC ASPIRA_HAS_OPENCV)
    target_link_libraries(aspira_app PUBLIC ${OpenCV_LIBS})
    target_include_directories(aspira_app PUBLIC ${OpenCV_INCLUDE_DIRS})
endif()
