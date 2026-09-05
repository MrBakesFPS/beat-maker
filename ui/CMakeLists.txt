# ui module — currently also owns the application entry point.
# Surface and Depth views live in subdirectories and share one component tree.

juce_add_gui_app(BeatMaker
    PRODUCT_NAME    "Beat Maker"
    COMPANY_NAME    "Beat Maker"
    BUNDLE_ID       com.beatmaker.app
    VERSION         ${PROJECT_VERSION})

target_sources(BeatMaker PRIVATE
    main.cpp
    surface/TransportBar.cpp
    surface/TrackArea.cpp
    surface/StepSequencer.cpp
    surface/LoopBrowser.cpp
    surface/PianoRoll.cpp
    surface/SmartControls.cpp
    depth/BounceDialog.cpp
    depth/EditToolbar.cpp
    depth/FadesDialog.cpp
    depth/MixerView.cpp
    depth/IOSetupDialog.cpp
    depth/GroupDialog.cpp)

target_compile_definitions(BeatMaker PRIVATE
    BEATMAKER_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BeatMaker,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BeatMaker,JUCE_VERSION>")

target_link_libraries(BeatMaker PRIVATE
    beatmaker_engine
    beatmaker_model
    beatmaker_persistence
    juce::juce_audio_utils
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
