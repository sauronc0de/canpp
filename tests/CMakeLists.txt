add_executable(canpp_dbc_tests dbc_tests.cpp)
target_compile_options(canpp_dbc_tests PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-UNDEBUG>
  $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/U;NDEBUG>
)
target_link_libraries(canpp_dbc_tests
  PRIVATE
    canpp::trace_core
    canpp::project_options
)
target_compile_features(canpp_dbc_tests PRIVATE cxx_std_23)
set_target_properties(canpp_dbc_tests PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
add_test(NAME canpp_dbc_tests COMMAND canpp_dbc_tests)

add_executable(canpp_query_tests query_tests.cpp)
target_compile_options(canpp_query_tests PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-UNDEBUG>
  $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/U;NDEBUG>
)
target_link_libraries(canpp_query_tests
  PRIVATE
    canpp::trace_core
    canpp::project_options
)
target_compile_features(canpp_query_tests PRIVATE cxx_std_23)
set_target_properties(canpp_query_tests PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
add_test(NAME canpp_query_tests COMMAND canpp_query_tests)

add_executable(canpp_plot_tests plot_tests.cpp)
target_compile_options(canpp_plot_tests PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-UNDEBUG>
  $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/U;NDEBUG>
)
target_link_libraries(canpp_plot_tests
  PRIVATE
    canpp::trace_core
    canpp::project_options
)
target_compile_features(canpp_plot_tests PRIVATE cxx_std_23)
set_target_properties(canpp_plot_tests PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
add_test(NAME canpp_plot_tests COMMAND canpp_plot_tests)
