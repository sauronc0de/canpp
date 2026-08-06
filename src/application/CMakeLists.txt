add_library(canpp_application STATIC
  src/command_parser.cpp
  src/command_executor.cpp
)
add_library(canpp::application ALIAS canpp_application)

target_include_directories(canpp_application
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(canpp_application
  PUBLIC
    canpp::trace_core
    canpp::project_options
)
target_compile_features(canpp_application PUBLIC cxx_std_23)
set_target_properties(canpp_application PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
