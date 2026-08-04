add_library(canpp_trace_core STATIC
  src/session.cpp
)
add_library(canpp::trace_core ALIAS canpp_trace_core)

target_include_directories(canpp_trace_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(canpp_trace_core
  PUBLIC
    canpp::trace
    canpp::protocol_can
)

target_compile_features(canpp_trace_core PUBLIC cxx_std_23)
set_target_properties(canpp_trace_core PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
