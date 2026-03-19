add_library(can_export STATIC src/exporter.cpp)
target_include_directories(can_export PUBLIC include)
target_link_libraries(can_export PUBLIC can_core can_decode)
