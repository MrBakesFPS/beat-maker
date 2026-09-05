cmake_minimum_required(VERSION 3.22)
project(BeatMaker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BEATMAKER_BUILD_TESTS "Build unit tests" ON)

# TODO (Phase 0): add JUCE via FetchContent or add_subdirectory(third_party/JUCE)
# include(FetchContent)
# FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4)
# FetchContent_MakeAvailable(JUCE)

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
