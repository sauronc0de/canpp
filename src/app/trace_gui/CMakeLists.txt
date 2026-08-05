add_executable(CanppGraph main.cpp)
target_link_libraries(CanppGraph
  PRIVATE
    canpp::render
    canpp::project_options
)
target_compile_features(CanppGraph PRIVATE cxx_std_23)
set_target_properties(CanppGraph PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
