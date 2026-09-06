# persistence module — file import/export and (later) session load/save
add_library(beatmaker_persistence INTERFACE)

target_sources(beatmaker_persistence INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/AudioFileLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LoopLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionImport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SampleProjects.cpp)

target_include_directories(beatmaker_persistence INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(beatmaker_persistence INTERFACE
    beatmaker_engine
    beatmaker_model
    beatmaker_plugins
    juce::juce_audio_basics
    juce::juce_audio_formats)
