#include "StemExportDialog.h"
#include <Freeze.h>

namespace beatmaker::ui
{

StemExportDialog::StemExportDialog (const model::Session& s, bool hasCycle) : session (s)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    int y = 0;
    for (int i : model::Freeze::renderableTracks (session))
    {
        const auto& t = session.getTracks()[(size_t) i];
        auto* toggle = trackToggles.add (new juce::ToggleButton (juce::String (i + 1) + "  " + t.name + (t.isAux() ? "  (aux)" : "")));
        toggle->setToggleState (true, juce::dontSendNotification);
        toggle->setBounds (4, y, 380, 24);
        holder.addAndMakeVisible (toggle);
        trackIndices.push_back (i);
        y += 24;
    }
    holder.setSize (400, juce::jmax (24, y));

    for (auto* t : { &auxToggle, &masterToggle, &cycleToggle }) addAndMakeVisible (t);
    cycleToggle.setEnabled (hasCycle);
    for (auto* l : { &formatLabel, &depthLabel, &tailLabel }) { l->setColour (juce::Label::textColourId, theme::textDim); addAndMakeVisible (l); }
    addAndMakeVisible (formatBox);
    formatBox.addItem ("WAV", 1); formatBox.addItem ("AIFF", 2); formatBox.addItem ("FLAC", 3);
    formatBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (depthBox);
    depthBox.addItem ("16-bit", 1); depthBox.addItem ("24-bit", 2); depthBox.addItem ("32-bit float", 3);
    depthBox.setSelectedId (2, juce::dontSendNotification);
    addAndMakeVisible (tailSlider);
    tailSlider.setRange (0.0, 10.0, 0.5);
    tailSlider.setValue (2.0, juce::dontSendNotification);
    tailSlider.setTextValueSuffix (" s");
    for (auto* b : { &allButton, &noneButton, &exportButton, &cancelButton }) addAndMakeVisible (b);
    allButton.onClick = [this] { for (auto* t : trackToggles) t->setToggleState (true, juce::dontSendNotification); };
    noneButton.onClick = [this] { for (auto* t : trackToggles) t->setToggleState (false, juce::dontSendNotification); };
    exportButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    exportButton.onClick = [this]
    {
        Request r;
        for (int k = 0; k < trackToggles.size(); ++k) if (trackToggles[k]->getToggleState()) r.tracks.push_back (trackIndices[(size_t) k]);
        r.includeAuxReturns = auxToggle.getToggleState();
        r.throughMasterInserts = masterToggle.getToggleState();
        r.cycleRange = cycleToggle.getToggleState();
        const engine::BounceSettings::Format formats[] = { engine::BounceSettings::Format::wav, engine::BounceSettings::Format::aiff, engine::BounceSettings::Format::flac };
        r.settings.format = formats[juce::jlimit (0, 2, formatBox.getSelectedId() - 1)];
        const int depths[] = { 16, 24, 32 };
        r.settings.bitDepth = depths[juce::jlimit (0, 2, depthBox.getSelectedId() - 1)];
        if (! engine::BounceSettings::supportsBitDepth (r.settings.format, r.settings.bitDepth)) r.settings.bitDepth = 24;
        r.settings.tailSeconds = tailSlider.getValue();
        r.settings.trimTail = false;   // stems must stay the same length
        r.settings.normalize = false;
        if (onExport) onExport (r);
    };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    setSize (preferredWidth, preferredHeight);
}

void StemExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Export Stems", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Each track is rendered on its own, post-fader with automation, all the same length so they line up.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void StemExportDialog::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (40);
    auto buttons = area.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (80)); buttons.removeFromRight (6);
    exportButton.setBounds (buttons.removeFromRight (130));
    allButton.setBounds (buttons.removeFromLeft (50)); buttons.removeFromLeft (4); noneButton.setBounds (buttons.removeFromLeft (60));
    area.removeFromBottom (8);
    auto options = area.removeFromBottom (24 * 3 + 30 * 3);
    auto row = [&] (juce::Label& l, juce::Component& c) { auto r = options.removeFromTop (28); l.setBounds (r.removeFromLeft (80)); c.setBounds (r); options.removeFromTop (2); };
    row (formatLabel, formatBox); row (depthLabel, depthBox); row (tailLabel, tailSlider);
    auxToggle.setBounds (options.removeFromTop (24)); masterToggle.setBounds (options.removeFromTop (24)); cycleToggle.setBounds (options.removeFromTop (24));
    area.removeFromBottom (8);
    viewport.setBounds (area);
}

} // namespace beatmaker::ui
