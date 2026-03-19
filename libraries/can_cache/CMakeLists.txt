add_library(can_cache INTERFACE)
target_include_directories(can_cache INTERFACE include)
target_link_libraries(can_cache INTERFACE can_core can_index)

