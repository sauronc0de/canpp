add_library(can_app STATIC src/can_app.cpp)
target_include_directories(can_app PUBLIC include)
target_link_libraries(can_app
  PUBLIC
    can_core
    can_reader_api
    can_dbc
    can_decode
    can_query
    can_index
    can_cache
    can_export
    can_script_api
    logger
)

# Temporary compatibility alias while the codebase transitions away from the
# older target name.
add_library(canapp ALIAS can_app)

