#pragma once

#include <JuceHeader.h>

class MeterComponent : public juce::Component
{
public:
    enum class Type
    {
        inputOutput,
        gainReduction
    };

    MeterComponent (juce::String meterName, Type meterType);

    void setDbValue (float newDb) noexcept;

private:
    void paint (juce::Graphics& g) override;

    float dbToNormalised (float db) const noexcept;
    juce::String formatValueText (float db) const;

    juce::String name;
    Type type = Type::inputOutput;

    float minDb = -60.0f;
    float maxDb = 0.0f;
    float targetDb = -60.0f;
    float displayedDb = -60.0f;
    float lastPaintedDb = -60.0f;
};
