add_executable(canpp_trace_cli main.cpp)
target_link_libraries(canpp_trace_cli PRIVATE canpp::trace_core)
target_compile_features(canpp_trace_cli PRIVATE cxx_std_23)
set_target_properties(canpp_trace_cli PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
