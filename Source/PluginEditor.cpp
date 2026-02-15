#include "PluginEditor.h"
#include "Parameters.h"

TwoCCompressorAudioProcessorEditor::TwoCCompressorAudioProcessorEditor (TwoCCompressorAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      inputMeter ("IN", MeterComponent::Type::inputOutput),
      grMeter ("GR", MeterComponent::Type::gainReduction),
      outputMeter ("OUT", MeterComponent::Type::inputOutput)
{
    setSize (980, 560);

    setupControl (controls[0], "Input", Parameters::IDs::inputDb);
    setupControl (controls[1], "Threshold", Parameters::IDs::thresholdDb);
    setupControl (controls[2], "Ratio", Parameters::IDs::ratio);
    setupControl (controls[3], "Attack", Parameters::IDs::attackMs);
    setupControl (controls[4], "Release", Parameters::IDs::releaseMs);
    setupControl (controls[5], "SC HPF", Parameters::IDs::scHpfHz);
    setupControl (controls[6], "Knee", Parameters::IDs::kneeDb);
    setupControl (controls[7], "Makeup", Parameters::IDs::makeupDb);
    setupControl (controls[8], "Drive", Parameters::IDs::satDrive);
    setupControl (controls[9], "Sat Mix", Parameters::IDs::satMix);
    setupControl (controls[10], "Mix", Parameters::IDs::mix);
    setupControl (controls[11], "Output", Parameters::IDs::outputDb);

    osModeLabel.setText ("Oversampling", juce::dontSendNotification);
    osModeLabel.setJustificationType (juce::Justification::centredLeft);
    osModeLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    osModeLabel.setFont (juce::FontOptions { 14.0f, juce::Font::bold });
    addAndMakeVisible (osModeLabel);

    osModeBox.addItem ("Off", 1);
    osModeBox.addItem ("2x", 2);
    osModeBox.addItem ("4x", 3);
    osModeBox.setJustificationType (juce::Justification::centred);
    osModeBox.setColour (juce::ComboBox::backgroundColourId, juce::Colours::white.withAlpha (0.08f));
    osModeBox.setColour (juce::ComboBox::textColourId, juce::Colours::white.withAlpha (0.9f));
    osModeBox.setColour (juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha (0.2f));
    addAndMakeVisible (osModeBox);

    osModeAttachment = std::make_unique<ComboAttachment> (processor.getAPVTS(), Parameters::IDs::osMode, osModeBox);

    scHpfEnabledButton.setButtonText ("SC HPF On");
    scHpfEnabledButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white.withAlpha (0.9f));
    addAndMakeVisible (scHpfEnabledButton);
    scHpfEnabledAttachment = std::make_unique<ButtonAttachment> (processor.getAPVTS(), Parameters::IDs::scHpfEnabled, scHpfEnabledButton);

    meterTitle.setText ("Meters", juce::dontSendNotification);
    meterTitle.setJustificationType (juce::Justification::centredLeft);
    meterTitle.setFont (juce::FontOptions { 16.0f, juce::Font::bold });
    addAndMakeVisible (meterTitle);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (outputMeter);

    timerCallback();
    startTimerHz (60);
}

void TwoCCompressorAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient bg (
        juce::Colour::fromRGB (18, 23, 31), 0.0f, 0.0f,
        juce::Colour::fromRGB (7, 9, 14), 0.0f, static_cast<float> (getHeight()),
        false);

    g.setGradientFill (bg);
    g.fillAll();

    auto bounds = getLocalBounds().reduced (16);
    auto controlsArea = bounds.removeFromLeft (bounds.proportionOfWidth (0.78f));

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (controlsArea.toFloat(), 12.0f);
    g.fillRoundedRectangle (bounds.toFloat(), 12.0f);

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (controlsArea.toFloat(), 12.0f, 1.0f);
    g.drawRoundedRectangle (bounds.toFloat(), 12.0f, 1.0f);
}

void TwoCCompressorAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    auto controlsArea = bounds.removeFromLeft (bounds.proportionOfWidth (0.78f)).reduced (14);
    auto meterArea = bounds.reduced (14);

    auto osRow = controlsArea.removeFromBottom (34);
    controlsArea.removeFromBottom (10);

    juce::Grid grid;
    grid.templateRows = {
        juce::Grid::TrackInfo (1_fr),
        juce::Grid::TrackInfo (1_fr),
        juce::Grid::TrackInfo (1_fr)
    };
    grid.templateColumns = {
        juce::Grid::TrackInfo (1_fr),
        juce::Grid::TrackInfo (1_fr),
        juce::Grid::TrackInfo (1_fr),
        juce::Grid::TrackInfo (1_fr)
    };
    grid.rowGap = juce::Grid::Px { 12.0f };
    grid.columnGap = juce::Grid::Px { 12.0f };

    juce::Array<juce::GridItem> items;
    for (auto& control : controls)
        items.add (juce::GridItem (control.slider));

    grid.items = items;
    grid.performLayout (controlsArea);

    for (auto& control : controls)
    {
        auto area = control.slider.getBounds();
        control.label.setBounds (area.removeFromTop (22));
        control.slider.setBounds (area);
    }

    const auto toggleWidth = 92;
    const auto toggleHeight = 20;
    const auto toggleX = controls[5].slider.getRight() - toggleWidth;
    const auto toggleY = controls[5].slider.getY() - toggleHeight - 2;
    scHpfEnabledButton.setBounds (toggleX, toggleY, toggleWidth, toggleHeight);

    osModeLabel.setBounds (osRow.removeFromLeft (120));
    osRow.removeFromLeft (8);
    osModeBox.setBounds (osRow.removeFromLeft (120));

    meterTitle.setBounds (meterArea.removeFromTop (28));
    meterArea.removeFromTop (10);

    juce::Grid meterGrid;
    meterGrid.templateRows = { juce::Grid::TrackInfo (1_fr) };
    meterGrid.templateColumns = {
        juce::Grid::TrackInfo (1_fr),
        juce::Grid::TrackInfo (1_fr),
        juce::Grid::TrackInfo (1_fr)
    };
    meterGrid.columnGap = juce::Grid::Px { 10.0f };
    meterGrid.items = {
        juce::GridItem (inputMeter),
        juce::GridItem (grMeter),
        juce::GridItem (outputMeter)
    };
    meterGrid.performLayout (meterArea);
}

void TwoCCompressorAudioProcessorEditor::timerCallback()
{
    const auto in = processor.inputMeterDb.load (std::memory_order_relaxed);
    const auto gr = processor.gainReductionDb.load (std::memory_order_relaxed);
    const auto out = processor.outputMeterDb.load (std::memory_order_relaxed);

    inputMeter.setTargetDb (in);
    grMeter.setTargetDb (gr);
    outputMeter.setTargetDb (out);

    constexpr auto dt = 1.0f / 60.0f;
    inputMeter.tickSmoothing (dt);
    grMeter.tickSmoothing (dt);
    outputMeter.tickSmoothing (dt);
}

void TwoCCompressorAudioProcessorEditor::setupControl (ParameterControl& control, const juce::String& name, const juce::String& parameterID)
{
    control.label.setText (name, juce::dontSendNotification);
    control.label.setJustificationType (juce::Justification::centred);
    control.label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.9f));
    control.label.setFont (juce::FontOptions { 14.0f, juce::Font::bold });
    addAndMakeVisible (control.label);

    control.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    control.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 20);
    control.slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::white.withAlpha (0.2f));
    control.slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB (75, 174, 224));
    control.slider.setColour (juce::Slider::thumbColourId, juce::Colours::white.withAlpha (0.95f));
    control.slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.9f));
    control.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    control.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::white.withAlpha (0.08f));

    if (parameterID == Parameters::IDs::scHpfHz)
    {
        control.slider.textFromValueFunction = [] (double value)
        {
            if (value <= 0.0)
                return juce::String ("Off");

            return juce::String (juce::roundToInt (value)) + " Hz";
        };

        control.slider.valueFromTextFunction = [] (const juce::String& text)
        {
            const auto trimmed = text.trim();

            if (trimmed.isEmpty() || trimmed.equalsIgnoreCase ("off"))
                return 0.0;

            const auto hzText = trimmed.upToFirstOccurrenceOf ("hz", false, false).trim();
            const auto hz = hzText.getDoubleValue();

            if (hz <= 0.0)
                return 0.0;

            return juce::jlimit (20.0, 250.0, hz);
        };
    }

    addAndMakeVisible (control.slider);

    control.attachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID, control.slider);
}

