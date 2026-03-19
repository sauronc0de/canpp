add_library(can_dbc STATIC src/database.cpp)
target_include_directories(can_dbc PUBLIC include)
target_link_libraries(can_dbc PUBLIC can_core)
