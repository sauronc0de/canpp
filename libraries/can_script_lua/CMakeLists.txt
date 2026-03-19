add_library(can_script_lua STATIC src/lua_engine.cpp)
target_include_directories(can_script_lua PUBLIC include)
target_link_libraries(can_script_lua PUBLIC can_script_api lua_common)

