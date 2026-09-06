# engine module — real-time audio. Header + source interface library so its
# sources compile into whichever target consumes it (app, tests) and the JUCE
# modules it needs are only compiled once per final target.
add_library(beatmaker_engine INTERFACE)

target_sources(beatmaker_engine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/bounce/Bouncer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/DrumKitFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/DrumMachine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Effects.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Resampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/TimeStretch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Transients.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/Instruments.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/PitchCorrection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/graph/AudioGraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/io/AudioEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/io/Recorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/metering/Loudness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sync/MidiSync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sync/MidiSyncController.cpp)

target_include_directories(beatmaker_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# juce_dsp's SIMDRegister<int64> does not compile with this GCC/glibc combination
# (juce::int64 is long long, the native-ops specialisation is for int64_t = long);
# the convolution engine does not need it.
target_compile_definitions(beatmaker_engine INTERFACE JUCE_USE_SIMD=0)

target_link_libraries(beatmaker_engine INTERFACE
    rubberband
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_events)
