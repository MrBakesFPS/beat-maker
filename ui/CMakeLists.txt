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
    depth/GroupDialog.cpp
    depth/BeatDetectiveDialog.cpp
    depth/MemoryLocationsWindow.cpp
    depth/NewSessionDialog.cpp
    depth/InstrumentChooser.cpp
    depth/DrumKitChooser.cpp
    depth/CommandPalette.cpp
    depth/PreferencesWindow.cpp
    depth/MidiEventList.cpp
    depth/SyncDialog.cpp
    depth/ScriptConsole.cpp
    depth/StemExportDialog.cpp
    depth/ImportSessionDialog.cpp
    depth/WelcomeWindow.cpp
    depth/TourOverlay.cpp
    depth/TutorialWindow.cpp
    depth/ShortcutsWindow.cpp
    depth/SystemUsageWindow.cpp
    depth/AafExportDialog.cpp
    depth/CrashReportWindow.cpp
    shared/CrashReporter.cpp
    shared/Preferences.cpp)

target_compile_definitions(BeatMaker PRIVATE
    BEATMAKER_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    BEATMAKER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    BEATMAKER_VERSION_STRING="${BEATMAKER_VERSION_STRING}"
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BeatMaker,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BeatMaker,JUCE_VERSION>")

if(UNIX AND NOT APPLE)
    target_link_options(BeatMaker PRIVATE -rdynamic)   # symbol names in crash backtraces
endif()

target_link_libraries(BeatMaker PRIVATE
    beatmaker_engine
    beatmaker_model
    beatmaker_persistence
    beatmaker_plugins
    beatmaker_scripting
    juce::juce_audio_utils
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
