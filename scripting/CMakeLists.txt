# scripting module — the Lua API (beatmaker.session / transport / app).
add_library(beatmaker_scripting INTERFACE)

target_sources(beatmaker_scripting INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/LuaEngine.cpp)

target_include_directories(beatmaker_scripting INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(beatmaker_scripting INTERFACE
    beatmaker_model
    lua_static)
