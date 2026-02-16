#pragma once

#include <JuceHeader.h>

#include <cmath>

class MeterBallistics
{
public:
    void prepare (double newSampleRate, float newAttackMs, float newReleaseMs)
    {
        sampleRate = juce::jmax (1.0, newSampleRate);
        setTimes (newAttackMs, newReleaseMs);
    }

    void setTimes (float newAttackMs, float newReleaseMs)
    {
        attackMs = juce::jmax (0.1f, newAttackMs);
        releaseMs = juce::jmax (0.1f, newReleaseMs);

        attackCoeff = makeCoeff (attackMs);
        releaseCoeff = makeCoeff (releaseMs);
    }

    void reset (float initialDb = -100.0f) noexcept
    {
        stateDb = initialDb;
    }

    float processSample (float targetDb) noexcept
    {
        const auto coeff = targetDb > stateDb ? attackCoeff : releaseCoeff;
        stateDb = coeff * stateDb + (1.0f - coeff) * targetDb;
        return stateDb;
    }

    float getCurrentDb() const noexcept
    {
        return stateDb;
    }

private:
    float makeCoeff (float timeMs) const noexcept
    {
        const auto seconds = juce::jmax (0.00001, static_cast<double> (timeMs) * 0.001);
        return std::exp (-1.0f / static_cast<float> (seconds * sampleRate));
    }

    double sampleRate = 44100.0;
    float attackMs = 10.0f;
    float releaseMs = 300.0f;
    float attackCoeff = 0.0f;
    float releaseCoeff = 0.0f;
    float stateDb = -100.0f;
};
