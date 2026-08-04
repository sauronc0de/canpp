option(CANPP_BUILD_TRACE_CLI "Build the communication trace CLI" ON)
if(CANPP_BUILD_TRACE_CLI)
  add_subdirectory(trace_cli)
endif()
