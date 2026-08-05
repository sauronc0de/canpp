add_executable(Canpp main.cpp)
target_link_libraries(Canpp
  PRIVATE
    canpp::render
    canpp::project_options
)
target_compile_features(Canpp PRIVATE cxx_std_23)
set_target_properties(Canpp PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
