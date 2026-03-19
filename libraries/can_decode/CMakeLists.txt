add_library(can_decode INTERFACE)
target_include_directories(can_decode INTERFACE include)
target_link_libraries(can_decode INTERFACE can_core can_dbc)

