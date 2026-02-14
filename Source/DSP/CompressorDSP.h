#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

class CompressorDSP
{
public:
    struct Parameters
    {
        float thresholdDb = -18.0f;
        float ratio = 4.0f;
        float attackMs = 10.0f;
        float releaseMs = 100.0f;
        float kneeDb = 6.0f;
    };

    void init (double newSampleRate, int /*maxBlockSize*/)
    {
        sampleRate = juce::jmax (1.0, newSampleRate);
        reset();
        updateTimeConstants();
    }

    void reset()
    {
        rmsState.fill (0.0f);
        gainReductionEnvelopeDb = 0.0f;
        smoothedGainLinear = 1.0f;
        lastGainReductionDb = 0.0f;
    }

    void setParameters (const Parameters& newParameters)
    {
        parameters = newParameters;
        parameters.ratio = juce::jmax (1.0f, parameters.ratio);
        parameters.attackMs = juce::jmax (0.01f, parameters.attackMs);
        parameters.releaseMs = juce::jmax (0.01f, parameters.releaseMs);
        parameters.kneeDb = juce::jmax (0.0f, parameters.kneeDb);

        updateTimeConstants();
    }

    void processBlock (juce::AudioBuffer<float>& buffer)
    {
        const auto numChannels = juce::jmin (2, buffer.getNumChannels());
        const auto numSamples = buffer.getNumSamples();

        if (numChannels <= 0 || numSamples <= 0)
        {
            lastGainReductionDb = 0.0f;
            return;
        }

        auto peakGainReductionInBlock = 0.0f;

        for (auto sample = 0; sample < numSamples; ++sample)
        {
            auto linkedRms = 0.0f;

            for (auto channel = 0; channel < numChannels; ++channel)
            {
                const auto x = buffer.getSample (channel, sample);
                const auto squared = x * x;

                auto& state = rmsState[static_cast<size_t> (channel)];
                state = rmsCoeff * state + (1.0f - rmsCoeff) * squared;

                linkedRms = juce::jmax (linkedRms, std::sqrt (state));
            }

            const auto detectorDb = juce::Decibels::gainToDecibels (linkedRms, -120.0f);
            const auto targetGainReductionDb = computeGainReductionDb (detectorDb);

            float grCoeff = attackCoeff;

            if (targetGainReductionDb <= gainReductionEnvelopeDb)
            {
                const auto releaseBlend = smoothstep ((gainReductionEnvelopeDb - smallGrDb) / (largeGrDb - smallGrDb));
                grCoeff = juce::jmap (releaseBlend, releaseSlowCoeff, releaseFastCoeff);
            }

            gainReductionEnvelopeDb = grCoeff * gainReductionEnvelopeDb + (1.0f - grCoeff) * targetGainReductionDb;

            const auto targetGainLinear = juce::Decibels::decibelsToGain (-gainReductionEnvelopeDb);
            smoothedGainLinear = gainSmoothCoeff * smoothedGainLinear + (1.0f - gainSmoothCoeff) * targetGainLinear;

            for (auto channel = 0; channel < numChannels; ++channel)
                buffer.setSample (channel, sample, buffer.getSample (channel, sample) * smoothedGainLinear);

            peakGainReductionInBlock = juce::jmax (peakGainReductionInBlock, gainReductionEnvelopeDb);
        }

        lastGainReductionDb = juce::jmax (0.0f, peakGainReductionInBlock);
    }

    float getLastGainReductionDb() const noexcept
    {
        return lastGainReductionDb;
    }

private:
    static float coefficientFromMs (float timeMs, double sr)
    {
        const auto seconds = juce::jmax (0.00001, static_cast<double> (timeMs) * 0.001);
        return std::exp (-1.0f / static_cast<float> (seconds * sr));
    }

    static float smoothstep (float x) noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, x);
        return t * t * (3.0f - 2.0f * t);
    }

    float computeGainReductionDb (float inputDb) const
    {
        const auto threshold = parameters.thresholdDb;
        const auto ratio = juce::jmax (1.0f, parameters.ratio);
        const auto knee = juce::jmax (0.0f, parameters.kneeDb);

        auto outputDb = inputDb;

        if (knee <= 0.0f)
        {
            if (inputDb > threshold)
                outputDb = threshold + ((inputDb - threshold) / ratio);
        }
        else
        {
            const auto lowerKnee = threshold - 0.5f * knee;
            const auto upperKnee = threshold + 0.5f * knee;

            if (inputDb < lowerKnee)
            {
                outputDb = inputDb;
            }
            else if (inputDb > upperKnee)
            {
                outputDb = threshold + ((inputDb - threshold) / ratio);
            }
            else
            {
                const auto x = inputDb - lowerKnee;
                const auto slopeDelta = (1.0f / ratio) - 1.0f;
                outputDb = inputDb + slopeDelta * ((x * x) / (2.0f * knee));
            }
        }

        return juce::jmax (0.0f, inputDb - outputDb);
    }

    void updateTimeConstants()
    {
        attackCoeff = coefficientFromMs (parameters.attackMs, sampleRate);

        constexpr auto releaseScale = 4.0f;
        const auto releaseFastMs = juce::jlimit (5.0f, 2000.0f, parameters.releaseMs / releaseScale);
        const auto releaseSlowMs = juce::jlimit (5.0f, 2000.0f, parameters.releaseMs * releaseScale);

        releaseFastCoeff = coefficientFromMs (releaseFastMs, sampleRate);
        releaseSlowCoeff = coefficientFromMs (releaseSlowMs, sampleRate);

        constexpr auto rmsWindowMs = 10.0f;
        rmsCoeff = coefficientFromMs (rmsWindowMs, sampleRate);

        constexpr auto gainSmoothingMs = 2.0f;
        gainSmoothCoeff = coefficientFromMs (gainSmoothingMs, sampleRate);
    }

    Parameters parameters;

    double sampleRate = 44100.0;

    float attackCoeff = 0.0f;
    float releaseFastCoeff = 0.0f;
    float releaseSlowCoeff = 0.0f;
    float rmsCoeff = 0.0f;
    float gainSmoothCoeff = 0.0f;

    std::array<float, 2> rmsState { 0.0f, 0.0f };

    float gainReductionEnvelopeDb = 0.0f;
    float smoothedGainLinear = 1.0f;
    float lastGainReductionDb = 0.0f;

    static constexpr float smallGrDb = 3.0f;
    static constexpr float largeGrDb = 10.0f;
};
