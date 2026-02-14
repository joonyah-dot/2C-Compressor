#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

class TwoCCompressorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit TwoCCompressorAudioProcessorEditor (TwoCCompressorAudioProcessor&);
    ~TwoCCompressorAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct ParameterControl
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setupControl (ParameterControl& control, const juce::String& name, const juce::String& parameterID);
    static juce::String formatMeterDb (float db);

    TwoCCompressorAudioProcessor& processor;

    std::array<ParameterControl, 9> controls;

    juce::Label meterTitle;
    juce::Label inputMeterLabel;
    juce::Label grMeterLabel;
    juce::Label outputMeterLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TwoCCompressorAudioProcessorEditor)
};
