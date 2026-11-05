# Goblet — live mixing of three WAV fragments
# Build instructions: see README.md (short version: ./build.sh)

cmake_minimum_required(VERSION 3.22)

# Apple Silicon only, macOS 12 or newer.
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")

project(Goblet VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- JUCE --------------------------------------------------------------------
# By default JUCE is downloaded automatically (first build takes a few minutes).
# Set GOBLET_JUCE_PATH to use a local copy instead (for example Effet's
# build/_deps/juce-src, to skip the download).
set(GOBLET_JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout")

if(GOBLET_JUCE_PATH)
    add_subdirectory(${GOBLET_JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- The app -----------------------------------------------------------------
juce_add_gui_app(Goblet
    PRODUCT_NAME        "Goblet"
    COMPANY_NAME        "Goblet"
    BUNDLE_ID           "com.goblet.app"
    VERSION             ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED FALSE)

juce_generate_juce_header(Goblet)

set(GOBLET_ENGINE_SOURCES
    Source/Engine/SincKernel.cpp
    Source/Engine/SourceFile.cpp
    Source/Engine/DeckPlayer.cpp
    Source/Engine/PluginChain.cpp
    Source/Engine/MidiControl.cpp
    Source/Engine/Mixer.cpp)

target_sources(Goblet PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    ${GOBLET_ENGINE_SOURCES}
    Source/UI/SourceStrip.cpp
    Source/UI/DeckView.cpp
    Source/UI/Meters.cpp
    Source/UI/PluginHost.cpp
    Source/UI/MidiMappings.cpp
    Source/UI/MidiPanel.cpp
    Source/UI/CapturesPanel.cpp)

target_compile_definitions(Goblet PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Goblet,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Goblet,JUCE_VERSION>"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LADSPA=0)

if(APPLE)
    target_compile_definitions(Goblet PRIVATE JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(Goblet
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Measurement program (./build.sh tests) ---------------------------------
# Checks the deck player (passthrough, playhead, loop seam, declick) and
# measures its CPU cost per preset at the usual rate ratios. Off by default.
option(GOBLET_BUILD_TESTS "Build the GobletTests measurement program" OFF)

if(GOBLET_BUILD_TESTS)
    juce_add_console_app(GobletTests PRODUCT_NAME "GobletTests")
    juce_generate_juce_header(GobletTests)
    target_sources(GobletTests PRIVATE Tests/DeckTests.cpp ${GOBLET_ENGINE_SOURCES})
    target_compile_definitions(GobletTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(GobletTests
        PRIVATE
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endif()
