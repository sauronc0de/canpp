function(add_canpp_test target)
  add_executable(${target} ${ARGN})
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${target} PRIVATE doctest::doctest_with_main)
  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_canpp_test(utest_can_core utest_can_core.cpp)
target_link_libraries(utest_can_core PRIVATE can_core)

add_canpp_test(utest_can_dbc utest_can_dbc.cpp)
target_link_libraries(utest_can_dbc PRIVATE can_dbc)

add_canpp_test(utest_can_decode utest_can_decode.cpp)
target_link_libraries(utest_can_decode PRIVATE can_decode)

add_canpp_test(utest_can_readers_text utest_can_readers_text.cpp)
target_link_libraries(utest_can_readers_text PRIVATE can_readers_text)

add_canpp_test(utest_can_cache utest_can_cache.cpp)
target_link_libraries(utest_can_cache PRIVATE can_cache)

add_canpp_test(utest_can_query utest_can_query.cpp)
target_link_libraries(utest_can_query PRIVATE can_query can_readers_text)

add_canpp_test(itest_can_app itest_can_app.cpp)
target_link_libraries(itest_can_app PRIVATE can_app)

add_canpp_test(utest_can_export utest_can_export.cpp)
target_link_libraries(utest_can_export PRIVATE can_export)

add_canpp_test(utest_can_index utest_can_index.cpp)
target_link_libraries(utest_can_index PRIVATE can_index)

add_canpp_test(utest_can_script_api utest_can_script_api.cpp)
target_link_libraries(utest_can_script_api PRIVATE can_script_api)

add_canpp_test(utest_can_script_lua utest_can_script_lua.cpp)
target_link_libraries(utest_can_script_lua PRIVATE can_script_lua)

add_canpp_test(utest_can_readers_binary utest_can_readers_binary.cpp)
target_link_libraries(utest_can_readers_binary PRIVATE can_readers_binary)
