cmake_minimum_required(VERSION 3.22)

project(Ambience VERSION 1.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(AMBIENCE_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(AMBIENCE_BUILD_TESTS "Build the DSP tests and the offline renderer" ON)

# ------------------------------------------------------------------------------
# DSP library (pure C++, no JUCE): used by the plugin, the tests and the CLI renderer.
add_library(ambience_dsp STATIC
    Source/dsp/SpatialEngine.cpp)
target_include_directories(ambience_dsp PUBLIC Source)
target_compile_features(ambience_dsp PUBLIC cxx_std_20)
set_target_properties(ambience_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(ambience_dsp PRIVATE /W4)
else()
    target_compile_options(ambience_dsp PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-float-equal)
endif()

if(AMBIENCE_BUILD_TESTS)
    enable_testing()
    add_executable(ambience_tests tests/EngineTests.cpp)
    target_link_libraries(ambience_tests PRIVATE ambience_dsp)
    add_test(NAME ambience_tests COMMAND ambience_tests)

    add_executable(ambience_render tools/RenderCli.cpp)
    target_link_libraries(ambience_render PRIVATE ambience_dsp)
endif()

# ------------------------------------------------------------------------------
if(AMBIENCE_BUILD_PLUGIN)
    include(FetchContent)
    # Use -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE for an existing checkout.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    # Company / plugin codes: 4 chars. AU/GarageBand: first letter upper case, the rest lower case.
    set(AMBIENCE_COMPANY_NAME "SAMS" CACHE STRING "Company name shown in hosts")
    set(AMBIENCE_MANUFACTURER_CODE "Ambi" CACHE STRING "4-char manufacturer code (placeholder, change before release)")

    # GitHub repository queried by "Check for updates" (must be public for anonymous access).
    set(AMBIENCE_GITHUB_REPO "ffsfdfhbdg01/AMBIENCE-PLUGIN-VST" CACHE STRING "owner/repo for update checks")

    # Release builds may pass -DAMBIENCE_VERSION=<tag version>; empty = project() version.
    set(AMBIENCE_VERSION "" CACHE STRING "Override of the plugin version shown to hosts")
    if(AMBIENCE_VERSION STREQUAL "")
        set(AMBIENCE_PLUGIN_VERSION "${PROJECT_VERSION}")
    else()
        set(AMBIENCE_PLUGIN_VERSION "${AMBIENCE_VERSION}")
    endif()

    juce_add_plugin(Ambience
        VERSION ${AMBIENCE_PLUGIN_VERSION}
        COMPANY_NAME "${AMBIENCE_COMPANY_NAME}"
        PLUGIN_MANUFACTURER_CODE ${AMBIENCE_MANUFACTURER_CODE}
        PLUGIN_CODE Ambc
        FORMATS VST3 AU Standalone
        PRODUCT_NAME "SAMS AMBIENCE CONTROL"
        BUNDLE_ID "com.ambienceaudio.ambience"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        NEEDS_CURL TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Spatial
        AU_MAIN_TYPE kAudioUnitType_Effect)

    juce_add_binary_data(AmbienceData SOURCES
        Resources/Inter-Regular.ttf
        Resources/Inter-SemiBold.ttf)

    target_sources(Ambience PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PresetManager.cpp
        Source/UpdateChecker.cpp
        Source/ui/LookAndFeel.cpp
        Source/ui/StageView.cpp
        Source/ui/Meters.cpp)

    target_compile_definitions(Ambience PUBLIC
        JUCE_WEB_BROWSER=0
        AMBIENCE_GITHUB_REPO="${AMBIENCE_GITHUB_REPO}"
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    if(NOT MSVC)
        target_compile_options(Ambience PRIVATE -Wno-float-equal)
    endif()

    target_link_libraries(Ambience
        PRIVATE
            ambience_dsp
            AmbienceData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
