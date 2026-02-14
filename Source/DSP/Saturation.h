#pragma once

#include <JuceHeader.h>
#include <cmath>

class Saturation
{
public:
    void processBlock (juce::dsp::AudioBlock<float>& block, float drive) const noexcept
    {
        if (drive <= 0.0f)
            return;

        const auto preGain = 1.0f + drive * 24.0f;
        const auto outputNormalise = 1.0f / std::tanh (preGain);

        for (size_t channel = 0; channel < block.getNumChannels(); ++channel)
        {
            auto* samples = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
                samples[sample] = std::tanh (samples[sample] * preGain) * outputNormalise;
        }
    }
};
