cmake_minimum_required(VERSION 3.22)
project(BeatMaker VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BEATMAKER_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# Rubber Band Library (GPL) for Elastic-style time stretching and pitch
# shifting. Built from its single-file amalgamation: no Meson, no external
# FFT or resampler.
FetchContent_Declare(rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG        v4.0.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(rubberband)
# CLAP plugin API headers (MIT) for the CLAP host.
FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.10
    GIT_SHALLOW    TRUE)
FetchContent_Populate(clap)
add_library(clap_headers INTERFACE)
target_include_directories(clap_headers INTERFACE ${clap_SOURCE_DIR}/include)

add_library(rubberband STATIC ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp)
target_include_directories(rubberband PUBLIC ${rubberband_SOURCE_DIR})
target_compile_options(rubberband PRIVATE -w)   # third-party: warnings are theirs
set_target_properties(rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(engine)
add_subdirectory(model)
add_subdirectory(persistence)
add_subdirectory(plugins)
add_subdirectory(scripting)
add_subdirectory(ui)

if(BEATMAKER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
