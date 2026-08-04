add_library(canpp_trace STATIC
  src/binary_trace.cpp
)
add_library(canpp::trace ALIAS canpp_trace)

target_include_directories(canpp_trace
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(canpp_trace PUBLIC cxx_std_23)
set_target_properties(canpp_trace PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
