# persistence module
add_library(beatmaker_persistence INTERFACE)
target_include_directories(beatmaker_persistence INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# TODO: convert to STATIC and add sources as they are written
