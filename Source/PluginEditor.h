#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "UI/MeterComponent.h"

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
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct ParameterControl
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setupControl (ParameterControl& control, const juce::String& name, const juce::String& parameterID);
    void updateTimingControlState();

    TwoCCompressorAudioProcessor& processor;

    std::array<ParameterControl, 12> controls;

    juce::Label osModeLabel;
    juce::ComboBox osModeBox;
    std::unique_ptr<ComboAttachment> osModeAttachment;

    juce::Label timingModeLabel;
    juce::ComboBox timingModeBox;
    std::unique_ptr<ComboAttachment> timingModeAttachment;

    juce::ToggleButton scHpfEnabledButton;
    std::unique_ptr<ButtonAttachment> scHpfEnabledAttachment;

    juce::Label meterTitle;
    MeterComponent inputMeter;
    MeterComponent grMeter;
    MeterComponent outputMeter;

    std::atomic<float>* timingModeParam = nullptr;
    bool manualTimingEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TwoCCompressorAudioProcessorEditor)
};
