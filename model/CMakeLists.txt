# model module — the undoable session document
add_library(beatmaker_model INTERFACE)

target_sources(beatmaker_model INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderSnapshotBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ClipEdits.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MixerCommands.cpp)

target_include_directories(beatmaker_model INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(beatmaker_model INTERFACE
    beatmaker_engine
    juce::juce_audio_basics
    juce::juce_graphics)
