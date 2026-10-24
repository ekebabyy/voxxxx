cmake_minimum_required(VERSION 3.22)

# Link the MSVC runtime statically so the plugin does not depend on the VC++ redistributable
# being installed on the user's machine (it is loaded into a DAW process).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Voxform VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VOXFORM_BUILD_TESTS "Build the stand-alone DSP test executable" ON)

# ---------------------------------------------------------------------------------------------
# JUCE (downloaded automatically at configure time)
# ---------------------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.16-security
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------------------------
juce_add_plugin(Voxform
    COMPANY_NAME                "Voxform"
    PRODUCT_NAME                "Voxform"
    PLUGIN_MANUFACTURER_CODE    Vxfm
    PLUGIN_CODE                 Vxf1
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Instrument Synth
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE)   # copying into Program Files needs admin rights; see README

target_sources(Voxform PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.h
    Source/VoxEngine.h
    Source/VoxReverb.h)

target_include_directories(Voxform PRIVATE Source)

target_compile_definitions(Voxform PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Voxform
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------
# DSP test (no JUCE): renders audio scenarios and checks them
# ---------------------------------------------------------------------------------------------
if(VOXFORM_BUILD_TESTS)
    add_executable(voxform_dsp_test tests/dsp_test.cpp)
    target_include_directories(voxform_dsp_test PRIVATE Source)
endif()
