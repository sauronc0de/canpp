add_library(can_query INTERFACE)
target_include_directories(can_query INTERFACE include)
target_link_libraries(can_query INTERFACE can_core can_decode can_reader_api)

