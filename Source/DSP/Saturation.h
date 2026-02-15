#pragma once

#include <JuceHeader.h>
#include <cmath>

class Saturation
{
public:
    void processInPlace (juce::dsp::AudioBlock<float>& block, float drive, float mix) const noexcept
    {
        if (drive <= 0.0f || mix <= 0.0f)
            return;

        const auto wetMix = juce::jlimit (0.0f, 1.0f, mix);
        const auto dryMix = 1.0f - wetMix;

        // Gentler drive law for finer low-end control.
        const auto driveClamped = juce::jlimit (0.0f, 1.0f, drive);
        const auto driveT = driveClamped * driveClamped;
        const auto driveDb = 12.0f * driveT;

        const auto inputGain = juce::Decibels::decibelsToGain (driveDb);

        // Partial auto compensation keeps tone changes while limiting loudness jumps.
        constexpr auto compensationAmount = 0.70f;
        const auto outputGain = juce::Decibels::decibelsToGain (-driveDb * compensationAmount);

        for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto* samples = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto dry = samples[sample];
                const auto driven = dry * inputGain;
                const auto shaped = std::tanh (driven);
                const auto wet = shaped * outputGain;
                samples[sample] = wet * wetMix + dry * dryMix;
            }
        }
    }
};
