add_executable(Canpp main.cpp)
# Keep the former target name available to downstream CMake consumers.
add_executable(canpp_trace_cli ALIAS Canpp)
target_link_libraries(Canpp
  PRIVATE
    canpp::trace_core
    canpp::project_options
)
target_compile_features(Canpp PRIVATE cxx_std_23)

# Readline is optional so the CLI remains usable on minimal platforms.
pkg_check_modules(CANPP_READLINE QUIET IMPORTED_TARGET readline)
if(CANPP_READLINE_FOUND)
  target_link_libraries(Canpp PRIVATE PkgConfig::CANPP_READLINE)
  target_compile_definitions(Canpp PRIVATE CANPP_TRACE_CLI_HAS_READLINE=1)
endif()

set_target_properties(Canpp PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
