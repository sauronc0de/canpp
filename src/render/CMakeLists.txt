add_library(canpp_render INTERFACE)
add_library(canpp::render ALIAS canpp_render)

target_link_libraries(canpp_render
  INTERFACE
    canpp::trace_core
    canpp_implot
    imgui
    OpenGL::GL
    SDL2::SDL2
)

target_include_directories(canpp_render INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
