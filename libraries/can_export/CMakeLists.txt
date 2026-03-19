add_library(can_export INTERFACE)
target_include_directories(can_export INTERFACE include)
target_link_libraries(can_export INTERFACE can_core can_decode)

