option(CANPP_BUILD_TRACE_CLI "Build the communication trace CLI" ON)
if(CANPP_BUILD_TRACE_CLI)
  add_subdirectory(trace_cli)
endif()

if(ENABLE_RENDER)
  add_subdirectory(trace_gui)
  if(TARGET Canpp AND TARGET CanppGraph)
    target_compile_definitions(Canpp PRIVATE
      CANPP_GUI_EXECUTABLE_NAME="CanppGraph"
    )
  endif()
endif()
