#include "BounceDialog.h"

namespace beatmaker::ui
{

BounceDialog::BounceDialog (Context context) : ctx (context)
{
    for (auto* l : { &rangeLabel, &formatLabel, &depthLabel, &tailLabel })
    {
        addAndMakeVisible (l);
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centredRight);
    }

    addAndMakeVisible (rangeBox);
    rangeBox.addItem ("Whole arrangement", 1);
    if (ctx.cycleEndSeconds > ctx.cycleStartSeconds)
        rangeBox.addItem ("Cycle range", 2);
    rangeBox.setSelectedId (1, juce::dontSendNotification);
    rangeBox.onChange = [this] { updateInfo(); };

    addAndMakeVisible (formatBox);
    formatBox.addItem ("WAV", 1);
    formatBox.addItem ("AIFF", 2);
    formatBox.addItem ("FLAC", 3);
    formatBox.setSelectedId (1, juce::dontSendNotification);
    formatBox.onChange = [this] { updateBitDepths(); };

    addAndMakeVisible (depthBox);
    updateBitDepths();

    addAndMakeVisible (tailSlider);
    tailSlider.setRange (0.0, 10.0, 0.1);
    tailSlider.setValue (2.0, juce::dontSendNotification);
    tailSlider.setTextValueSuffix (" s");
    tailSlider.setTooltip ("Keep rendering after the range so drum and effect tails ring out");

    addAndMakeVisible (trimToggle);
    trimToggle.setToggleState (true, juce::dontSendNotification);

    addAndMakeVisible (normalizeToggle);

    addAndMakeVisible (infoLabel);
    infoLabel.setColour (juce::Label::textColourId, theme::textDim);
    infoLabel.setFont (juce::FontOptions (12.0f));
    infoLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (bounceButton);
    bounceButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.5f));
    bounceButton.onClick = [this] { if (onBounce) onBounce (buildSettings()); };

    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };

    updateInfo();
    setSize (preferredWidth, preferredHeight);
}

void BounceDialog::updateBitDepths()
{
    const auto format = static_cast<engine::BounceSettings::Format> (formatBox.getSelectedId() - 1);
    const int previous = depthBox.getSelectedId();

    depthBox.clear (juce::dontSendNotification);
    for (int bits : { 16, 24, 32 })
        if (engine::BounceSettings::supportsBitDepth (format, bits))
            depthBox.addItem (bits == 32 ? "32-bit float" : juce::String (bits) + "-bit", bits);

    depthBox.setSelectedId (depthBox.indexOfItemId (previous) >= 0 ? previous : 24, juce::dontSendNotification);
}

void BounceDialog::updateInfo()
{
    const auto s = buildSettings();
    const double seconds = (double) (s.endSample - s.startSample) / s.sampleRate;
    infoLabel.setText (juce::String (seconds, 2) + " s  +  tail    "
                         + juce::String (ctx.sampleRate / 1000.0, 1) + " kHz    "
                         + juce::String (ctx.numOutputs) + " ch",
                       juce::dontSendNotification);
}

engine::BounceSettings BounceDialog::buildSettings() const
{
    engine::BounceSettings s;
    s.sampleRate  = ctx.sampleRate;
    s.numChannels = juce::jmax (1, ctx.numOutputs);
    s.format      = static_cast<engine::BounceSettings::Format> (formatBox.getSelectedId() - 1);
    s.bitDepth    = depthBox.getSelectedId() > 0 ? depthBox.getSelectedId() : 24;
    s.tailSeconds = tailSlider.getValue();
    s.trimTail    = trimToggle.getToggleState();
    s.normalize   = normalizeToggle.getToggleState();

    double start = 0.0, end = ctx.arrangementSeconds;
    if (rangeBox.getSelectedId() == 2) { start = ctx.cycleStartSeconds; end = ctx.cycleEndSeconds; }

    s.startSample = (juce::int64) std::llround (start * ctx.sampleRate);
    s.endSample   = (juce::int64) std::llround (end * ctx.sampleRate);
    return s;
}

void BounceDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void BounceDialog::resized()
{
    auto area = getLocalBounds().reduced (20);
    const int labelWidth = 90, rowHeight = 26, gap = 10;

    auto row = [&] (juce::Label& label, juce::Component& control)
    {
        auto r = area.removeFromTop (rowHeight);
        label.setBounds (r.removeFromLeft (labelWidth));
        r.removeFromLeft (10);
        control.setBounds (r);
        area.removeFromTop (gap);
    };

    row (rangeLabel, rangeBox);
    row (formatLabel, formatBox);
    row (depthLabel, depthBox);
    row (tailLabel, tailSlider);

    auto toggles = area.removeFromTop (rowHeight);
    toggles.removeFromLeft (labelWidth + 10);
    trimToggle.setBounds (toggles);
    area.removeFromTop (4);
    toggles = area.removeFromTop (rowHeight);
    toggles.removeFromLeft (labelWidth + 10);
    normalizeToggle.setBounds (toggles);
    area.removeFromTop (gap);

    auto buttons = area.removeFromBottom (30);
    bounceButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));

    infoLabel.setBounds (area.removeFromBottom (22));
}

} // namespace beatmaker::ui
