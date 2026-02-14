#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
float getPeakDb (const juce::AudioBuffer<float>& buffer, int channelsToMeasure)
{
    const auto numChannels = juce::jmin (channelsToMeasure, buffer.getNumChannels());

    if (numChannels <= 0 || buffer.getNumSamples() <= 0)
        return -100.0f;

    auto peak = 0.0f;

    for (auto ch = 0; ch < numChannels; ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    return juce::Decibels::gainToDecibels (peak, -100.0f);
}

float loadParam (const std::atomic<float>* parameter, float fallback) noexcept
{
    return parameter != nullptr ? parameter->load (std::memory_order_relaxed) : fallback;
}
}

TwoCCompressorAudioProcessor::TwoCCompressorAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", Parameters::createParameterLayout())
{
    cacheParameterPointers();
}

void TwoCCompressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    compressor.init (sampleRate, samplesPerBlock);
    dryBuffer.setSize (getTotalNumOutputChannels(), juce::jmax (1, samplesPerBlock), false, false, true);

    inputMeterDb.store (0.0f);
    outputMeterDb.store (0.0f);
    gainReductionDb.store (0.0f);
}

void TwoCCompressorAudioProcessor::releaseResources() {}

bool TwoCCompressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainIn == mainOut;
}

void TwoCCompressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numInputChannels = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();

    const auto inputDb = loadParam (inputDbParam, 0.0f);
    const auto thresholdDb = loadParam (thresholdDbParam, -18.0f);
    const auto ratio = loadParam (ratioParam, 4.0f);
    const auto attackMs = loadParam (attackMsParam, 10.0f);
    const auto releaseMs = loadParam (releaseMsParam, 100.0f);
    const auto scHpfHz = loadParam (scHpfHzParam, 0.0f);
    const auto kneeDb = loadParam (kneeDbParam, 6.0f);
    const auto makeupDb = loadParam (makeupDbParam, 0.0f);
    const auto mix = juce::jlimit (0.0f, 1.0f, loadParam (mixParam, 1.0f));
    const auto outputDb = loadParam (outputDbParam, 0.0f);

    inputMeterDb.store (getPeakDb (buffer, numInputChannels), std::memory_order_relaxed);

    const auto hasDryBufferCapacity = dryBuffer.getNumChannels() >= numOutputChannels
                                   && dryBuffer.getNumSamples() >= numSamples;
    const auto useDryMix = mix < 1.0f && hasDryBufferCapacity;

    if (useDryMix)
    {
        for (auto channel = 0; channel < numOutputChannels; ++channel)
        {
            if (channel < numInputChannels)
                dryBuffer.copyFrom (channel, 0, buffer, channel, 0, numSamples);
            else
                dryBuffer.clear (channel, 0, numSamples);
        }
    }

    for (auto channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    buffer.applyGain (juce::Decibels::decibelsToGain (inputDb));

    CompressorDSP::Parameters compressorParams;
    compressorParams.thresholdDb = thresholdDb;
    compressorParams.ratio = ratio;
    compressorParams.attackMs = attackMs;
    compressorParams.releaseMs = releaseMs;
    compressorParams.scHpfHz = scHpfHz;
    compressorParams.kneeDb = kneeDb;
    compressor.setParameters (compressorParams);
    compressor.processBlock (buffer);

    const auto postGain = juce::Decibels::decibelsToGain (makeupDb + outputDb);
    buffer.applyGain (postGain);

    if (useDryMix)
    {
        const auto dryGain = 1.0f - mix;

        for (auto channel = 0; channel < numOutputChannels; ++channel)
        {
            buffer.applyGain (channel, 0, numSamples, mix);
            buffer.addFrom (channel, 0, dryBuffer, channel, 0, numSamples, dryGain);
        }
    }

    outputMeterDb.store (getPeakDb (buffer, numOutputChannels), std::memory_order_relaxed);
    gainReductionDb.store (compressor.getLastGainReductionDb(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* TwoCCompressorAudioProcessor::createEditor()
{
    return new TwoCCompressorAudioProcessorEditor (*this);
}

void TwoCCompressorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto state = apvts.copyState(); const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TwoCCompressorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

void TwoCCompressorAudioProcessor::cacheParameterPointers()
{
    inputDbParam = apvts.getRawParameterValue (Parameters::IDs::inputDb);
    thresholdDbParam = apvts.getRawParameterValue (Parameters::IDs::thresholdDb);
    ratioParam = apvts.getRawParameterValue (Parameters::IDs::ratio);
    attackMsParam = apvts.getRawParameterValue (Parameters::IDs::attackMs);
    releaseMsParam = apvts.getRawParameterValue (Parameters::IDs::releaseMs);
    scHpfHzParam = apvts.getRawParameterValue (Parameters::IDs::scHpfHz);
    kneeDbParam = apvts.getRawParameterValue (Parameters::IDs::kneeDb);
    makeupDbParam = apvts.getRawParameterValue (Parameters::IDs::makeupDb);
    mixParam = apvts.getRawParameterValue (Parameters::IDs::mix);
    outputDbParam = apvts.getRawParameterValue (Parameters::IDs::outputDb);
}
