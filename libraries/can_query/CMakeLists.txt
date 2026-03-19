add_library(can_query STATIC src/query_executor.cpp)
target_include_directories(can_query PUBLIC include)
target_link_libraries(can_query PUBLIC can_core can_decode can_reader_api)
