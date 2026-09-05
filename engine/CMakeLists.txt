# engine module — real-time audio. Header + source interface library so its
# sources compile into whichever target consumes it (app, tests) and the JUCE
# modules it needs are only compiled once per final target.
add_library(beatmaker_engine INTERFACE)

target_sources(beatmaker_engine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/bounce/Bouncer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/DrumKitFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/DrumMachine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/graph/AudioGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/io/AudioEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/io/Recorder.cpp)

target_include_directories(beatmaker_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(beatmaker_engine INTERFACE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_events)
