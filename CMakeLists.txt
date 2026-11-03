cmake_minimum_required (VERSION 3.22)
project (DynEQ VERSION 1.0.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set (CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include (FetchContent)
FetchContent_Declare (juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable (juce)

juce_add_plugin (${PROJECT_NAME}
    COMPANY_NAME              "DSH Audio"
    PRODUCT_NAME              "DynEQ"
    VERSION                   "1.0.0"
    FORMATS                   VST3
    PLUGIN_MANUFACTURER_CODE  DshX
    PLUGIN_CODE               DynQ
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    SOURCES
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

target_include_directories (${PROJECT_NAME} PRIVATE Source)
juce_generate_juce_header (${PROJECT_NAME})
