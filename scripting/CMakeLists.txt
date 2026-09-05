# scripting module
add_library(beatmaker_scripting INTERFACE)
target_include_directories(beatmaker_scripting INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# TODO: convert to STATIC and add sources as they are written
