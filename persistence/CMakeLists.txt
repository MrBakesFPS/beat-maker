# persistence module — file import/export and (later) session load/save
add_library(beatmaker_persistence INTERFACE)

target_sources(beatmaker_persistence INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/AudioFileLoader.cpp)

target_include_directories(beatmaker_persistence INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(beatmaker_persistence INTERFACE
    juce::juce_audio_basics
    juce::juce_audio_formats)
