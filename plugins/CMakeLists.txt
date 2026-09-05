# plugins module
add_library(beatmaker_plugins INTERFACE)
target_include_directories(beatmaker_plugins INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# TODO: convert to STATIC and add sources as they are written
