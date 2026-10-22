cmake_minimum_required(VERSION 3.22)

project(DumbDockAmp VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------
# Build options
#   -DJUCE_DIR=/path/to/JUCE        use an existing JUCE 7.x checkout
#   -DDUMBDOCK_STANDALONE=OFF       build only the VST3 (skip Standalone app)
# If JUCE_DIR is not given and ./JUCE does not exist, JUCE 7.0.12 is
# downloaded automatically (needs git + internet on first configure).
# ------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to an existing JUCE checkout (optional)")
option(DUMBDOCK_STANDALONE "Also build the Standalone app" ON)

if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Using JUCE from ${JUCE_DIR}")
    add_subdirectory("${JUCE_DIR}" JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using JUCE from ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    add_subdirectory(JUCE)
else()
    message(STATUS "JUCE not found locally - downloading JUCE 7.0.12 ...")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        7.0.12
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(DUMBDOCK_FORMATS VST3)
if(DUMBDOCK_STANDALONE)
    list(APPEND DUMBDOCK_FORMATS Standalone)
endif()

# ------------------------------------------------------------------
# The DSP engine: plain C++17, no JUCE.  Everything between the host's audio
# buffer and the host's output lives here, and the headless tests in tests/
# build exactly these files (see tests/Makefile).
# ------------------------------------------------------------------
add_library(DumbDockEngine STATIC
    Source/Amps/TubeStage.cpp
    Source/Amps/ToneStack.cpp
    Source/Amps/PowerStage.cpp
    Source/Amps/Reverb.cpp
    Source/Amps/Amp001Model.cpp
    Source/Amps/Amp002Model.cpp
    Source/Cab/CabIrDesigner.cpp
    Source/Cab/CabinetSim.cpp
    Source/Engine/AmpEngine.cpp
)
target_include_directories(DumbDockEngine PUBLIC Source)
set_target_properties(DumbDockEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(DumbDockAmp
    COMPANY_NAME             "DorukAudio"
    PLUGIN_MANUFACTURER_CODE Drkd
    PLUGIN_CODE              DdAm
    FORMATS                  ${DUMBDOCK_FORMATS}
    PRODUCT_NAME             "DumbDock Amp"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    VST3_CATEGORIES          Fx Distortion
)

juce_generate_juce_header(DumbDockAmp)

target_sources(DumbDockAmp
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Parameters/ParameterLayout.cpp
        Source/Parameters/EngineBinding.cpp
        Source/Presets/PresetManager.cpp
)

target_include_directories(DumbDockAmp PRIVATE Source)

target_compile_definitions(DumbDockAmp
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(DumbDockAmp
    PRIVATE
        DumbDockEngine
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(DUMBDOCK_STANDALONE)
    target_link_libraries(DumbDockAmp PRIVATE juce::juce_audio_utils)
endif()

# ------------------------------------------------------------------
# Optional headless self-test (instantiates the real processor, runs audio,
# MIDI, state round-trips and renders the editor + welcome popup to PNG):
#   cmake -B build -DDUMBDOCK_BUILD_TESTS=ON && cmake --build build --target DumbDockHostTest
# ------------------------------------------------------------------
option(DUMBDOCK_BUILD_TESTS "Build the headless host self-test" OFF)
if(DUMBDOCK_BUILD_TESTS)
    juce_add_console_app(DumbDockHostTest PRODUCT_NAME "DumbDockHostTest")
    juce_generate_juce_header(DumbDockHostTest)
    target_sources(DumbDockHostTest PRIVATE
        tests/plugin_host_test.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Source/Parameters/ParameterLayout.cpp Source/Parameters/EngineBinding.cpp
        Source/Presets/PresetManager.cpp)
    target_include_directories(DumbDockHostTest PRIVATE Source)
    target_compile_definitions(DumbDockHostTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="DumbDock Amp")
    target_link_libraries(DumbDockHostTest PRIVATE
        DumbDockEngine
        juce::juce_audio_processors juce::juce_audio_basics juce::juce_gui_basics juce::juce_graphics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
