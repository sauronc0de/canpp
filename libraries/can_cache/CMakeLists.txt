add_library(can_cache STATIC src/cache_reader.cpp)
target_include_directories(can_cache PUBLIC include)
target_link_libraries(can_cache PUBLIC can_core can_index)
