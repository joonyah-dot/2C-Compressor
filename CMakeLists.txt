cmake_minimum_required(VERSION 3.15)

project(TwoCCompressor VERSION 0.1.0)

add_subdirectory(extern/JUCE)

juce_add_plugin(TwoCCompressor
    COMPANY_NAME "KyleAudio"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "2C-Compressor"
)

# Generates JuceHeader.h + sets up include paths correctly
juce_generate_juce_header(TwoCCompressor)

# Beginner-safe: avoids VST3/VST2 replacement automation warnings
target_compile_definitions(TwoCCompressor PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_VST2_SDK=0
)

target_sources(TwoCCompressor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginEntry.cpp
    Source/Parameters.cpp
    Source/Parameters.h
    Source/DSP/CompressorDSP.h
    Source/DSP/Saturation.h
    Source/DSP/EnvelopeFollower.h
    Source/DSP/LevelDetector.h
)

target_compile_features(TwoCCompressor PRIVATE cxx_std_17)

target_link_libraries(TwoCCompressor PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra

    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
