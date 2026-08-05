option(CANPP_BUILD_TRACE_CLI "Build the communication trace CLI" ON)
if(CANPP_BUILD_TRACE_CLI)
  add_subdirectory(trace_cli)
endif()

if(ENABLE_RENDER)
  add_subdirectory(trace_gui)
  if(TARGET canpp_trace_cli AND TARGET Canpp)
    target_compile_definitions(canpp_trace_cli PRIVATE
      CANPP_GUI_EXECUTABLE_NAME="Canpp"
    )
  endif()
endif()
