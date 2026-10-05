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
    instruments_test.cpp
    time_stretch_test.cpp
    sidechain_convolution_test.cpp
    pitch_correction_test.cpp
    beat_detective_test.cpp
    session_file_test.cpp
    preferences_test.cpp
    midi_sync_test.cpp
    lua_test.cpp
    freeze_stems_test.cpp
    session_import_test.cpp
    onboarding_test.cpp
    theme_test.cpp
    performance_test.cpp
    aaf_export_test.cpp
    ../ui/shared/Preferences.cpp
    ../ui/shared/CrashReporter.cpp
    crash_reporter_test.cpp
    tempo_test.cpp
    note_edits_test.cpp
    step_edits_test.cpp
    clip_loop_test.cpp
    clip_join_test.cpp
    pan_test.cpp
    clip_edit_test.cpp
    fades_test.cpp
    mixer_test.cpp
    automation_test.cpp
    adc_test.cpp
    metering_test.cpp
    groups_test.cpp
    playlists_test.cpp
    trim_master_test.cpp
    clip_gain_scrub_pencil_test.cpp
    plugin_host_test.cpp
    clap_host_test.cpp
    effects_suite_test.cpp
    recorder_test.cpp
    bounce_test.cpp
    loop_library_test.cpp
    audio_file_loader_test.cpp)

# A tiny CLAP plugin (.clap shared library) the host tests load with dlopen / LoadLibrary.
add_library(fake_clap MODULE fake_clap/fake_clap_plugin.cpp)
target_link_libraries(fake_clap PRIVATE clap_headers)
set_target_properties(fake_clap PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "fake_gain")
add_dependencies(beatmaker_tests fake_clap)

target_compile_definitions(beatmaker_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    BEATMAKER_FAKE_CLAP_PATH="$<TARGET_FILE:fake_clap>")

# Tests deliberately compare exact sample values, so use plain warnings rather
# than juce_recommended_warning_flags (which enables -Wfloat-equal).
if(MSVC)
    target_compile_options(beatmaker_tests PRIVATE /W3)
else()
    target_compile_options(beatmaker_tests PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

target_link_libraries(beatmaker_tests PRIVATE
    beatmaker_engine
    beatmaker_model
    beatmaker_persistence
    beatmaker_plugins
    beatmaker_scripting
    Catch2::Catch2WithMain
    juce::juce_audio_formats
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_data_structures
    PUBLIC
    juce::juce_recommended_config_flags)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(beatmaker_tests)
