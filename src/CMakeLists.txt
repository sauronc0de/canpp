add_subdirectory(log)


add_library(canapp STATIC canapp.cpp)
target_include_directories(canapp PUBLIC inc)
target_link_libraries(canapp PUBLIC logger)