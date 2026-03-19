add_library(can_dbc INTERFACE)
target_include_directories(can_dbc INTERFACE include)
target_link_libraries(can_dbc INTERFACE can_core)

