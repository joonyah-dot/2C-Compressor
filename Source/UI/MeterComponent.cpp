#include "MeterComponent.h"

MeterComponent::MeterComponent (juce::String meterName, Type meterType)
    : name (std::move (meterName)), type (meterType)
{
    if (type == Type::gainReduction)
    {
        minDb = 0.0f;
        maxDb = 30.0f;
        targetDb = 0.0f;
        displayedDb = 0.0f;
        lastPaintedDb = 0.0f;
    }
}

void MeterComponent::setDbValue (float newDb) noexcept
{
    targetDb = juce::jlimit (minDb, maxDb, newDb);
    displayedDb = targetDb;

    // Repaint only when movement is meaningful to keep GUI work low.
    if (std::abs (displayedDb - lastPaintedDb) >= 0.05f)
    {
        lastPaintedDb = displayedDb;
        repaint();
    }
}

void MeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    auto content = getLocalBounds().reduced (8);
    auto nameArea = content.removeFromTop (20);
    auto valueArea = content.removeFromBottom (18);
    auto meterArea = content.reduced (4, 0);

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
    g.drawText (name, nameArea, juce::Justification::centred);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions { 12.0f });
    g.drawText (formatValueText (displayedDb), valueArea, juce::Justification::centred);

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (meterArea.toFloat(), 5.0f);

    const auto normalised = dbToNormalised (displayedDb);
    const auto fillHeight = meterArea.getHeight() * normalised;
    auto fillArea = meterArea.withTop (meterArea.getBottom() - juce::roundToInt (fillHeight));

    if (! fillArea.isEmpty())
    {
        const auto topColour = type == Type::gainReduction
                                  ? juce::Colour::fromRGB (240, 160, 75)
                                  : juce::Colour::fromRGB (99, 210, 160);
        const auto bottomColour = type == Type::gainReduction
                                     ? juce::Colour::fromRGB (208, 112, 52)
                                     : juce::Colour::fromRGB (45, 150, 110);

        const auto fillAreaF = fillArea.toFloat();
        juce::ColourGradient gradient (topColour, fillAreaF.getCentreX(), fillAreaF.getY(),
                                       bottomColour, fillAreaF.getCentreX(), fillAreaF.getBottom(),
                                       false);

        g.setGradientFill (gradient);
        g.fillRoundedRectangle (fillAreaF, 4.0f);
    }
}

float MeterComponent::dbToNormalised (float db) const noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / juce::jmax (0.0001f, maxDb - minDb));
}

juce::String MeterComponent::formatValueText (float db) const
{
    if (type == Type::gainReduction)
        return juce::String (juce::jlimit (0.0f, 99.9f, db), 1) + " dB";

    if (db <= minDb + 0.01f)
        return "-inf";

    return juce::String (db, 1) + " dB";
}
