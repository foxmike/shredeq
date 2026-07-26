# Populate the pinned JUCE dependency with: git submodule update --init --recursive
cmake_minimum_required(VERSION 3.22)

project(ShredEQ VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(
    SHREDEQ_CONFIGURE_ONLY
    "Configure the top-level project without requiring the JUCE submodule"
    OFF
)

if(SHREDEQ_CONFIGURE_ONLY)
    message(STATUS "ShredEQ configure-only mode enabled")
    return()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(
        FATAL_ERROR
        "The pinned JUCE submodule is missing. Populate JUCE before configuring a build."
    )
endif()

add_subdirectory(JUCE)

juce_add_plugin(ShredEQ
    PRODUCT_NAME ShredEQ
    COMPANY_NAME "nSpace Systems"
    VERSION 1.0.0
    PLUGIN_MANUFACTURER_CODE NSPC
    PLUGIN_CODE ShEq
    BUNDLE_ID com.nspacesystems.shredeq
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(ShredEQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(ShredEQ PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(ShredEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# TODO: Add a juce_add_binary_data target when factory presets are introduced.

enable_testing()

add_executable(ShredEQUnitTests
    tests/TestMain.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(ShredEQUnitTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_include_directories(ShredEQUnitTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ShredEQUnitTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

add_test(NAME ShredEQUnitTests COMMAND ShredEQUnitTests)
