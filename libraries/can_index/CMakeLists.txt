add_library(can_index STATIC src/trace_index.cpp)
target_include_directories(can_index PUBLIC include)
target_link_libraries(can_index PUBLIC can_core)
