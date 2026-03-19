add_library(can_decode STATIC src/decoder.cpp)
target_include_directories(can_decode PUBLIC include)
target_link_libraries(can_decode PUBLIC can_core can_dbc)
