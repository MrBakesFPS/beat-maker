# Tests: Catch2 unit tests plus offline render tests that exercise the same
# AudioGraph code path the live device callback uses.
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

juce_add_console_app(beatmaker_tests PRODUCT_NAME "beatmaker_tests")

target_sources(beatmaker_tests PRIVATE
    transport_test.cpp
    audio_graph_test.cpp
    session_test.cpp
    sequencer_test.cpp
    synth_test.cpp
    pan_test.cpp
    clip_edit_test.cpp
    fades_test.cpp
    mixer_test.cpp
    automation_test.cpp
    adc_test.cpp
    metering_test.cpp
    groups_test.cpp
    recorder_test.cpp
    bounce_test.cpp
    loop_library_test.cpp
    audio_file_loader_test.cpp)

target_compile_definitions(beatmaker_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# Tests deliberately compare exact sample values, so use plain warnings rather
# than juce_recommended_warning_flags (which enables -Wfloat-equal).
target_compile_options(beatmaker_tests PRIVATE -Wall -Wextra)

target_link_libraries(beatmaker_tests PRIVATE
    beatmaker_engine
    beatmaker_model
    beatmaker_persistence
    Catch2::Catch2WithMain
    juce::juce_audio_formats
    juce::juce_graphics
    PUBLIC
    juce::juce_recommended_config_flags)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(beatmaker_tests)
