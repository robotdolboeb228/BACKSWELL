cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel), macOS 11+. Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

project(Backswell VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Backswell
    COMPANY_NAME "Backswell Audio"
    BUNDLE_ID com.backswellaudio.backswell
    PLUGIN_MANUFACTURER_CODE Bksw
    PLUGIN_CODE Bswl
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Backswell"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Reverb
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(Backswell PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/RiserEngine.cpp
)

target_compile_definitions(Backswell PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Backswell
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
