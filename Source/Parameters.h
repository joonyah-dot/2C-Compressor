#pragma once

#include <JuceHeader.h>

namespace Parameters
{
namespace IDs
{
inline constexpr const char* inputDb = "inputDb";
inline constexpr const char* thresholdDb = "thresholdDb";
inline constexpr const char* ratio = "ratio";
inline constexpr const char* timingMode = "timingMode";
inline constexpr const char* character = "character";
inline constexpr const char* attackMs = "attackMs";
inline constexpr const char* releaseMs = "releaseMs";
inline constexpr const char* scHpfHz = "scHpfHz";
inline constexpr const char* scHpfEnabled = "scHpfEnabled";
inline constexpr const char* kneeDb = "kneeDb";
inline constexpr const char* makeupDb = "makeupDb";
inline constexpr const char* satDrive = "satDrive";
inline constexpr const char* satMix = "satMix";
inline constexpr const char* osMode = "osMode";
inline constexpr const char* mix = "mix";
inline constexpr const char* outputDb = "outputDb";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
