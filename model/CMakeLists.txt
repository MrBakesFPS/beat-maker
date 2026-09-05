# model module
add_library(beatmaker_model INTERFACE)
target_include_directories(beatmaker_model INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# TODO: convert to STATIC and add sources as they are written
