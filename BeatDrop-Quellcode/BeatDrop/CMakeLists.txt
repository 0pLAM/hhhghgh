cmake_minimum_required(VERSION 3.22)

project(BeatDrop VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# JUCE wird beim ersten Konfigurieren automatisch geladen (braucht Internet, einmalig).
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.16-security
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BeatDrop
    COMPANY_NAME                "BeatDrop"
    PLUGIN_MANUFACTURER_CODE    Bdrp
    PLUGIN_CODE                 Bdr1
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "BeatDrop"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
)

target_sources(BeatDrop PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/YtDlpClient.cpp
)

target_compile_definitions(BeatDrop PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

if (MSVC)
    target_compile_options(BeatDrop PRIVATE /utf-8)
endif()

target_link_libraries(BeatDrop
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
