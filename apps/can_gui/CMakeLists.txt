add_executable(can_gui src/main.cpp src/gui_application.cpp)
target_include_directories(can_gui PRIVATE include)
target_link_libraries(can_gui PRIVATE can_app log imgui SDL2::SDL2 OpenGL::GL GLEW::GLEW)

if(CANPP_ENABLE_DEVELOP_DATA)
  target_compile_definitions(can_gui PRIVATE
    CAN_GUI_ENABLE_DEVELOP_DATA=1
    CAN_GUI_DEVELOP_DATA_PATH="${CMAKE_SOURCE_DIR}/data")
endif()
