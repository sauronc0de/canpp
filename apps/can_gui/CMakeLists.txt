add_executable(can_gui src/main.cpp src/gui_application.cpp)
target_include_directories(can_gui PRIVATE include)
target_link_libraries(can_gui PRIVATE can_app log imgui)

