add_executable(canpp_trace_cli main.cpp)
target_link_libraries(canpp_trace_cli
  PRIVATE
    canpp::trace_core
    canpp::project_options
)
target_compile_features(canpp_trace_cli PRIVATE cxx_std_23)

# Readline is optional so the CLI remains usable on minimal platforms.
pkg_check_modules(CANPP_READLINE QUIET IMPORTED_TARGET readline)
if(CANPP_READLINE_FOUND)
  target_link_libraries(canpp_trace_cli PRIVATE PkgConfig::CANPP_READLINE)
  target_compile_definitions(canpp_trace_cli PRIVATE CANPP_TRACE_CLI_HAS_READLINE=1)
endif()

set_target_properties(canpp_trace_cli PROPERTIES
  CXX_EXTENSIONS OFF
  UNITY_BUILD OFF
)
