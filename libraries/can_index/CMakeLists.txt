add_library(can_index INTERFACE)
target_include_directories(can_index INTERFACE include)
target_link_libraries(can_index INTERFACE can_core)

