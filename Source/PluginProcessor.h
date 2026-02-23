#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

#include "DSP/CompressorDSP.h"
#include "DSP/MeterBallistics.h"
#include "DSP/Saturation.h"
#include "Parameters.h"

class TwoCCompressorAudioProcessor : public juce::AudioProcessor
{
public:
    TwoCCompressorAudioProcessor();
    ~TwoCCompressorAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts; }

    std::atomic<float> inputMeterDb { 0.0f };
    std::atomic<float> outputMeterDb { 0.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<int> osModeInUse { 0 };

private:
    void cacheParameterPointers();

    juce::AudioProcessorValueTreeState apvts;
    CompressorDSP compressor;
    Saturation saturation;

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> saturationDryBuffer;
    MeterBallistics inputMeterBallistics;
    MeterBallistics outputMeterBallistics;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling4x;

    std::atomic<float>* inputDbParam = nullptr;
    std::atomic<float>* thresholdDbParam = nullptr;
    std::atomic<float>* ratioParam = nullptr;
    std::atomic<float>* timingModeParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* attackMsParam = nullptr;
    std::atomic<float>* releaseMsParam = nullptr;
    std::atomic<float>* scHpfHzParam = nullptr;
    std::atomic<float>* scHpfEnabledParam = nullptr;
    std::atomic<float>* kneeDbParam = nullptr;
    std::atomic<float>* makeupDbParam = nullptr;
    std::atomic<float>* satDriveParam = nullptr;
    std::atomic<float>* satMixParam = nullptr;
    std::atomic<float>* osModeParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputDbParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TwoCCompressorAudioProcessor)
};
