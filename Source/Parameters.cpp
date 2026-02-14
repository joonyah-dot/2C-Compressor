#include "Parameters.h"

namespace
{
juce::NormalisableRange<float> makeRatioRange()
{
    return {
        1.0f,
        20.0f,
        [] (float start, float end, float normalised)
        {
            juce::ignoreUnused (start, end);
            const auto t = juce::jlimit (0.0f, 1.0f, normalised);

            if (t <= 0.5f)
                return 1.0f + (t / 0.5f) * 3.0f;

            return 4.0f + ((t - 0.5f) / 0.5f) * 16.0f;
        },
        [] (float start, float end, float value)
        {
            juce::ignoreUnused (start, end);
            const auto v = juce::jlimit (1.0f, 20.0f, value);

            if (v <= 4.0f)
                return ((v - 1.0f) / 3.0f) * 0.5f;

            return 0.5f + ((v - 4.0f) / 16.0f) * 0.5f;
        }
    };
}

juce::NormalisableRange<float> makeAttackRange()
{
    juce::NormalisableRange<float> range { 0.1f, 100.0f };
    range.setSkewForCentre (10.0f);
    return range;
}

juce::NormalisableRange<float> makeReleaseRange()
{
    juce::NormalisableRange<float> range { 5.0f, 2000.0f };
    range.setSkewForCentre (100.0f);
    return range;
}
}

juce::AudioProcessorValueTreeState::ParameterLayout Parameters::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.reserve (9);

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::inputDb, 1 }, "Input", juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value, 1) + " dB"; })));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::thresholdDb, 1 }, "Threshold", juce::NormalisableRange<float> { -60.0f, 0.0f }, -18.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value, 1) + " dB"; })));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::ratio, 1 }, "Ratio", makeRatioRange(), 4.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value, 2) + ":1"; })));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::attackMs, 1 }, "Attack", makeAttackRange(), 10.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value, 2) + " ms"; })));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::releaseMs, 1 }, "Release", makeReleaseRange(), 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value, 1) + " ms"; })));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::kneeDb, 1 }, "Knee", juce::NormalisableRange<float> { 0.0f, 12.0f }, 6.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value, 1) + " dB"; })));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::makeupDb, 1 }, "Makeup", juce::NormalisableRange<float> { -12.0f, 24.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value, 1) + " dB"; })));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::mix, 1 }, "Mix", juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value * 100.0f, 0) + " %"; })));

    parameters.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { IDs::outputDb, 1 }, "Output", juce::NormalisableRange<float> { -12.0f, 12.0f }, 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float value, int) { return juce::String (value, 1) + " dB"; })));

    return { parameters.begin(), parameters.end() };
}
