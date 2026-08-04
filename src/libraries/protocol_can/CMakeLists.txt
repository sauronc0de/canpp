add_library(canpp_protocol_can STATIC
  src/asc_importer.cpp
  src/can_codec.cpp
)
add_library(canpp::protocol_can ALIAS canpp_protocol_can)

target_include_directories(canpp_protocol_can
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(canpp_protocol_can PUBLIC canpp::trace)
target_compile_features(canpp_protocol_can PUBLIC cxx_std_23)
set_target_properties(canpp_protocol_can PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
