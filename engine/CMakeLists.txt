# engine module
add_library(beatmaker_engine INTERFACE)
target_include_directories(beatmaker_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# TODO: convert to STATIC and add sources as they are written
