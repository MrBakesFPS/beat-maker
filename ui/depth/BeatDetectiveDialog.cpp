#include "BeatDetectiveDialog.h"

namespace beatmaker::ui
{

BeatDetectiveDialog::BeatDetectiveDialog (const EditSettings& e) : edit (e)
{
    addSlider (sensitivitySlider, sensitivityLabel, 0.0, 100.0, 50.0, " %");
    addSlider (strengthSlider, strengthLabel, 0.0, 100.0, 100.0, " %");
    addSlider (excludeSlider, excludeLabel, 0.0, 100.0, 0.0, " %");
    addSlider (swingSlider, swingLabel, 0.0, 100.0, 0.0, " %");
    addSlider (crossfadeSlider, crossfadeLabel, 0.0, 50.0, 5.0, " ms");
    sensitivitySlider.setTooltip ("Transient detection sensitivity: higher finds softer hits");
    strengthSlider.setTooltip ("How far each clip moves toward its grid line");
    excludeSlider.setTooltip ("Clips already within this fraction of half a grid step are left alone");
    swingSlider.setTooltip ("Delays every second grid line; 100% is the triplet position");

    addAndMakeVisible (smoothingLabel);
    smoothingLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (smoothingBox);
    smoothingBox.addItem ("Fill Gaps", 1);
    smoothingBox.addItem ("Fill and Crossfade", 2);
    smoothingBox.setSelectedId (2, juce::dontSendNotification);

    addAndMakeVisible (gridLabel);
    gridLabel.setColour (juce::Label::textColourId, theme::textDim);
    gridLabel.setJustificationType (juce::Justification::centredLeft);

    for (auto* b : { &separateButton, &conformButton, &smoothButton, &allButton }) addAndMakeVisible (b);
    separateButton.setTooltip ("Separate the selected clips at every detected transient");
    conformButton.setTooltip ("Move each selected clip's start to the nearest grid line (uses the edit grid value)");
    smoothButton.setTooltip ("Fill the gaps between clips and crossfade the joins");
    allButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    separateButton.onClick = [this] { if (onSeparate) onSeparate(); };
    conformButton.onClick = [this] { if (onConform) onConform(); };
    smoothButton.onClick = [this] { if (onSmooth) onSmooth(); };
    allButton.onClick = [this] { if (onAll) onAll(); };

    addAndMakeVisible (status);
    status.setColour (juce::Label::textColourId, theme::textDim);
    status.setFont (juce::FontOptions (12.0f));
    setSize (preferredWidth, preferredHeight);
}

void BeatDetectiveDialog::addSlider (juce::Slider& s, juce::Label& l, double min, double max, double value, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    s.setRange (min, max, max > 60.0 ? 1.0 : 0.5);
    s.setValue (value, juce::dontSendNotification);
    s.setTextValueSuffix (suffix);
    s.setColour (juce::Slider::trackColourId, theme::accent);
    addAndMakeVisible (s);
    l.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (l);
}

model::ConformSettings BeatDetectiveDialog::conformSettings (double bpm) const
{
    model::ConformSettings c;
    c.bpm = bpm;
    c.gridBeats = edit.gridBeats;
    c.strength = (float) strengthSlider.getValue() * 0.01f;
    c.excludeWithin = (float) excludeSlider.getValue() * 0.01f;
    c.swing = (float) swingSlider.getValue() * 0.01f;
    return c;
}

model::SmoothingSettings BeatDetectiveDialog::smoothingSettings() const
{
    model::SmoothingSettings s;
    s.fillGaps = true;
    s.crossfade = smoothingBox.getSelectedId() == 2;
    s.crossfadeMs = crossfadeSlider.getValue();
    return s;
}

void BeatDetectiveDialog::setStatus (const juce::String& text) { status.setText (text, juce::dontSendNotification); }

void BeatDetectiveDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Beat Detective", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Works on the selected clips, or every clip of the selected track.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft);
    gridLabel.setText ("Grid: " + juce::String (edit.gridBeats >= 4.0 ? "bar" : edit.gridBeats >= 1.0 ? "1/4" : edit.gridBeats >= 0.5 ? "1/8" : "1/16")
                           + "  (change it in the edit toolbar)", juce::dontSendNotification);
}

void BeatDetectiveDialog::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (40);
    auto row = [&] (juce::Label& l, juce::Component& c)
    {
        auto r = area.removeFromTop (28);
        l.setBounds (r.removeFromLeft (110));
        c.setBounds (r);
        area.removeFromTop (4);
    };
    row (sensitivityLabel, sensitivitySlider);
    row (strengthLabel, strengthSlider);
    row (excludeLabel, excludeSlider);
    row (swingLabel, swingSlider);
    row (smoothingLabel, smoothingBox);
    row (crossfadeLabel, crossfadeSlider);
    gridLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (8);
    auto buttons = area.removeFromTop (28);
    const int w = (buttons.getWidth() - 12) / 4;
    separateButton.setBounds (buttons.removeFromLeft (w)); buttons.removeFromLeft (4);
    conformButton.setBounds (buttons.removeFromLeft (w));  buttons.removeFromLeft (4);
    smoothButton.setBounds (buttons.removeFromLeft (w));   buttons.removeFromLeft (4);
    allButton.setBounds (buttons);
    area.removeFromTop (8);
    status.setBounds (area.removeFromTop (20));
}

} // namespace beatmaker::ui
