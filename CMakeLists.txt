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
        Source/DSP/BiquadCoefficients.cpp
        Source/DSP/TPTFilter.cpp
        Source/DSP/EnvelopeFollower.cpp
        Source/DSP/DynamicProcessor.cpp
        Source/DSP/ParametricBand.cpp
        Source/DSP/ResponseComputer.cpp
        Source/DSP/TightCut.cpp
        Source/DSP/ChugStabilizer.cpp
        Source/DSP/MultibandCrossover.cpp
        Source/DSP/MultibandZone.cpp
        Source/DSP/MultibandEngine.cpp
        Source/DSP/PitchDetector.cpp
        Source/DSP/NoteFollower.cpp
        Source/Analysis/AnalysisFifo.cpp
        Source/Analysis/SpectralAnalysisEngine.cpp
        Source/DSP/ResonanceDetector.cpp
        Source/DSP/ResonanceSuppressor.cpp
        Source/DSP/MidSideRouter.cpp
        Source/DSP/SidechainAnalyzer.cpp
        Source/DSP/MaskingCarver.cpp
        Source/DSP/LookaheadDelay.cpp
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

set(MULTIBAND_TEST_SRC)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/MultibandTests.cpp")
    set(MULTIBAND_TEST_SRC tests/MultibandTests.cpp)
endif()

set(RESONANCE_TEST_SRC)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/ResonanceTests.cpp")
    set(RESONANCE_TEST_SRC tests/ResonanceTests.cpp)
endif()

set(MIDSIDE_TEST_SRC)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/MidSideTests.cpp")
    set(MIDSIDE_TEST_SRC tests/MidSideTests.cpp)
endif()

set(LOOKAHEAD_TEST_SRC)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/LookaheadTests.cpp")
    set(LOOKAHEAD_TEST_SRC tests/LookaheadTests.cpp)
endif()

add_executable(ShredEQUnitTests
    tests/TestMain.cpp
    tests/BiquadTests.cpp
    tests/ParametricBandTests.cpp
    tests/DynamicEQTests.cpp
    tests/TightCutTests.cpp
    tests/ChugStabilizerTests.cpp
    tests/DbgTests.cpp
    tests/PitchTrackingTests.cpp
    ${RESONANCE_TEST_SRC}
    ${MULTIBAND_TEST_SRC}
    ${MIDSIDE_TEST_SRC}
    ${LOOKAHEAD_TEST_SRC}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/BiquadCoefficients.cpp
    Source/DSP/TPTFilter.cpp
    Source/DSP/EnvelopeFollower.cpp
    Source/DSP/DynamicProcessor.cpp
    Source/DSP/ParametricBand.cpp
    Source/DSP/ResponseComputer.cpp
    Source/DSP/TightCut.cpp
    Source/DSP/ChugStabilizer.cpp
    Source/DSP/MultibandCrossover.cpp
    Source/DSP/MultibandZone.cpp
    Source/DSP/MultibandEngine.cpp
    Source/DSP/PitchDetector.cpp
    Source/DSP/NoteFollower.cpp
    Source/Analysis/AnalysisFifo.cpp
    Source/Analysis/SpectralAnalysisEngine.cpp
    Source/DSP/ResonanceDetector.cpp
    Source/DSP/ResonanceSuppressor.cpp
    Source/DSP/MidSideRouter.cpp
    Source/DSP/SidechainAnalyzer.cpp
    Source/DSP/MaskingCarver.cpp
    Source/DSP/LookaheadDelay.cpp
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
