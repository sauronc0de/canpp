add_executable(can_cli src/main.cpp)
target_link_libraries(can_cli PRIVATE can_app log imgui)

# Keep the historical top-level binary name available.
add_executable(${PROJECT_NAME} ALIAS can_cli)

