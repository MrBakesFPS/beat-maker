# plugins module — hosting VST3 / LV2 / AU plugins as inserts, with out-of-process scanning.
add_library(beatmaker_plugins INTERFACE)

target_sources(beatmaker_plugins INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginHost.cpp)

target_include_directories(beatmaker_plugins INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(beatmaker_plugins INTERFACE
    beatmaker_engine
    juce::juce_audio_processors)

# Hosted formats. CLAP has no JUCE host yet; AU applies on macOS only.
target_compile_definitions(beatmaker_plugins INTERFACE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST=0)
