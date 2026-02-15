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

        const auto preGain = 1.0f + drive * 24.0f;
        const auto outputNormalise = 1.0f / std::tanh (preGain);

        for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto* samples = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
            {
                const auto dry = samples[sample];
                const auto wet = std::tanh (dry * preGain) * outputNormalise;
                samples[sample] = wet * wetMix + dry * dryMix;
            }
        }
    }
};
