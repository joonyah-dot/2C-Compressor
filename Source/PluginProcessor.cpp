#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

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

int loadChoiceIndex (const std::atomic<float>* parameter, int fallback, int minValue, int maxValue) noexcept
{
    if (parameter == nullptr)
        return fallback;

    return juce::jlimit (minValue, maxValue, static_cast<int> (std::lround (parameter->load (std::memory_order_relaxed))));
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
    const auto numOutputChannels = juce::jmax (1, getTotalNumOutputChannels());
    const auto maxBlock = juce::jmax (1, samplesPerBlock);

    compressor.init (sampleRate, maxBlock);

    dryBuffer.setSize (numOutputChannels, maxBlock, false, false, true);
    saturationDryBuffer.setSize (numOutputChannels, maxBlock, false, false, true);

    const auto pendingMode = loadChoiceIndex (osModeParam, requestedOsMode.load (std::memory_order_relaxed), 0, 2);
    activeOsMode = pendingMode;
    requestedOsMode.store (pendingMode, std::memory_order_relaxed);
    oversamplingReinitRequested.store (false, std::memory_order_relaxed);

    const auto osChannels = static_cast<size_t> (numOutputChannels);
    const auto osBlock = static_cast<size_t> (maxBlock);

    oversampling2x = std::make_unique<juce::dsp::Oversampling<float>> (
        osChannels,
        1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        false);

    oversampling4x = std::make_unique<juce::dsp::Oversampling<float>> (
        osChannels,
        2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        false);

    oversampling2x->reset();
    oversampling2x->initProcessing (osBlock);

    oversampling4x->reset();
    oversampling4x->initProcessing (osBlock);

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
    const auto satDrive = juce::jlimit (0.0f, 1.0f, loadParam (satDriveParam, 0.0f));
    const auto satMix = juce::jlimit (0.0f, 1.0f, loadParam (satMixParam, 0.0f));
    const auto requestedMode = loadChoiceIndex (osModeParam, activeOsMode, 0, 2);
    const auto mix = juce::jlimit (0.0f, 1.0f, loadParam (mixParam, 1.0f));
    const auto outputDb = loadParam (outputDbParam, 0.0f);

    if (requestedMode != activeOsMode)
    {
        requestedOsMode.store (requestedMode, std::memory_order_relaxed);
        oversamplingReinitRequested.store (true, std::memory_order_relaxed);
        // Oversampling mode changes are applied in prepareToPlay().
        // In most hosts this means stop/restart transport (or reload audio engine).
    }

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

    // Wet path: Input trim -> Compressor -> Makeup -> Saturation
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

    buffer.applyGain (juce::Decibels::decibelsToGain (makeupDb));

    if (satDrive > 0.0001f && satMix > 0.0001f)
    {
        const auto useOversampling = activeOsMode > 0;
        auto effectiveSatMix = satMix;

        if (useOversampling && satMix < 0.999f)
        {
            const auto hasSatBlendBufferCapacity = saturationDryBuffer.getNumChannels() >= numOutputChannels
                                                && saturationDryBuffer.getNumSamples() >= numSamples;

            if (hasSatBlendBufferCapacity)
            {
                for (auto channel = 0; channel < numOutputChannels; ++channel)
                    saturationDryBuffer.copyFrom (channel, 0, buffer, channel, 0, numSamples);
            }
            else
            {
                effectiveSatMix = 1.0f;
            }
        }

        auto wetBlock = juce::dsp::AudioBlock<float> (buffer);

        if (useOversampling && activeOsMode == 1 && oversampling2x != nullptr)
        {
            auto upsampledBlock = oversampling2x->processSamplesUp (wetBlock);
            saturation.processInPlace (upsampledBlock, satDrive, 1.0f);
            oversampling2x->processSamplesDown (wetBlock);
        }
        else if (useOversampling && activeOsMode == 2 && oversampling4x != nullptr)
        {
            auto upsampledBlock = oversampling4x->processSamplesUp (wetBlock);
            saturation.processInPlace (upsampledBlock, satDrive, 1.0f);
            oversampling4x->processSamplesDown (wetBlock);
        }
        else
        {
            saturation.processInPlace (wetBlock, satDrive, effectiveSatMix);
            effectiveSatMix = 1.0f;
        }

        if (useOversampling && effectiveSatMix < 0.999f)
        {
            const auto cleanSatBlend = 1.0f - effectiveSatMix;

            for (auto channel = 0; channel < numOutputChannels; ++channel)
            {
                buffer.applyGain (channel, 0, numSamples, effectiveSatMix);
                buffer.addFrom (channel, 0, saturationDryBuffer, channel, 0, numSamples, cleanSatBlend);
            }
        }
    }

    // Then Wet/Dry mix.
    if (useDryMix)
    {
        const auto dryGain = 1.0f - mix;

        for (auto channel = 0; channel < numOutputChannels; ++channel)
        {
            buffer.applyGain (channel, 0, numSamples, mix);
            buffer.addFrom (channel, 0, dryBuffer, channel, 0, numSamples, dryGain);
        }
    }

    // Output trim is post wet/dry mix.
    buffer.applyGain (juce::Decibels::decibelsToGain (outputDb));

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
    satDriveParam = apvts.getRawParameterValue (Parameters::IDs::satDrive);
    satMixParam = apvts.getRawParameterValue (Parameters::IDs::satMix);
    osModeParam = apvts.getRawParameterValue (Parameters::IDs::osMode);
    mixParam = apvts.getRawParameterValue (Parameters::IDs::mix);
    outputDbParam = apvts.getRawParameterValue (Parameters::IDs::outputDb);

    requestedOsMode.store (loadChoiceIndex (osModeParam, 0, 0, 2), std::memory_order_relaxed);
    activeOsMode = requestedOsMode.load (std::memory_order_relaxed);
}
