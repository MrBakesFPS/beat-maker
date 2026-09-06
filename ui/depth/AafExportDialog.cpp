#include "AafExportDialog.h"

namespace beatmaker::ui
{

AafExportDialog::AafExportDialog (const model::Session& s) : session (s)
{
    trackIndices = persistence::AafExport::exportableTracks (session);
    for (int i : trackIndices)
    {
        const auto& t = session.getTracks()[(size_t) i];
        auto* toggle = trackToggles.add (new juce::ToggleButton (juce::String (i + 1) + "  " + t.name + (t.isFrozen() ? "  (frozen render)" : juce::String())));
        toggle->setToggleState (true, juce::dontSendNotification);
        toggle->setBounds (0, 24 * (trackToggles.size() - 1), 400, 22);
        holder.addAndMakeVisible (toggle);
    }
    holder.setSize (400, juce::jmax (24, 24 * trackToggles.size()));
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    for (auto* b : { &allButton, &noneButton, &exportButton, &cancelButton }) addAndMakeVisible (b);
    allButton.onClick = [this] { for (auto* t : trackToggles) t->setToggleState (true, juce::dontSendNotification); };
    noneButton.onClick = [this] { for (auto* t : trackToggles) t->setToggleState (false, juce::dontSendNotification); };
    for (auto* t : { &embedToggle, &linkToggle, &consolidateToggle, &wholeToggle, &markersToggle }) addAndMakeVisible (t);
    embedToggle.setRadioGroupId (1); linkToggle.setRadioGroupId (1); embedToggle.setToggleState (true, juce::dontSendNotification);
    consolidateToggle.setRadioGroupId (2); wholeToggle.setRadioGroupId (2); consolidateToggle.setToggleState (true, juce::dontSendNotification);
    markersToggle.setToggleState (true, juce::dontSendNotification);
    embedToggle.setTooltip ("One self-contained file; the other DAW extracts the audio");
    linkToggle.setTooltip ("The AAF references WAV files written to '<name> Media'; move the folder with the file");
    consolidateToggle.setTooltip ("Each clip becomes its own audio file with exactly what you hear, but no trim handles");
    wholeToggle.setTooltip ("Clips reference the full source files so they can be trimmed later; clip gain and fades are dropped");
    addAndMakeVisible (depthBox); addAndMakeVisible (fpsBox); addAndMakeVisible (depthLabel); addAndMakeVisible (fpsLabel);
    depthBox.addItem ("16-bit", 16); depthBox.addItem ("24-bit", 24); depthBox.setSelectedId (24, juce::dontSendNotification);
    fpsBox.addItem ("24 fps", 24); fpsBox.addItem ("25 fps", 25); fpsBox.addItem ("29.97 drop", 2997); fpsBox.addItem ("30 fps", 30); fpsBox.setSelectedId (30, juce::dontSendNotification);
    exportButton.onClick = [this]
    {
        persistence::AafExportOptions o;
        for (int i = 0; i < trackToggles.size(); ++i) if (trackToggles[i]->getToggleState()) o.tracks.push_back (trackIndices[(size_t) i]);
        if (o.tracks.empty()) return;
        o.embedAudio = embedToggle.getToggleState();
        o.consolidateClips = consolidateToggle.getToggleState();
        o.bitDepth = depthBox.getSelectedId();
        o.timecodeFps = fpsBox.getSelectedId() == 2997 ? 29.97 : (double) fpsBox.getSelectedId();
        o.includeMarkers = markersToggle.getToggleState();
        if (onExport) onExport (o);
    };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    setSize (preferredWidth, preferredHeight);
}

void AafExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Tracks", 16, 12, 200, 20, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (trackIndices.empty() ? "No audio tracks with clips (freeze an instrument track to include it)" : "Stereo tracks become .L and .R slots, as Pro Tools expects",
                16, 34, getWidth() - 32, 18, juce::Justification::centredLeft);
}

void AafExportDialog::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (40);
    viewport.setBounds (area.removeFromTop (130));
    area.removeFromTop (6);
    auto row = area.removeFromTop (24);
    allButton.setBounds (row.removeFromLeft (60)); row.removeFromLeft (6); noneButton.setBounds (row.removeFromLeft (60));
    area.removeFromTop (12);
    embedToggle.setBounds (area.removeFromTop (22)); linkToggle.setBounds (area.removeFromTop (22));
    area.removeFromTop (8);
    consolidateToggle.setBounds (area.removeFromTop (22)); wholeToggle.setBounds (area.removeFromTop (22));
    area.removeFromTop (8);
    row = area.removeFromTop (24);
    depthLabel.setBounds (row.removeFromLeft (70)); depthBox.setBounds (row.removeFromLeft (100)); row.removeFromLeft (16);
    fpsLabel.setBounds (row.removeFromLeft (70)); fpsBox.setBounds (row.removeFromLeft (110));
    area.removeFromTop (6);
    markersToggle.setBounds (area.removeFromTop (22));
    auto buttons = area.removeFromBottom (28);
    exportButton.setBounds (buttons.removeFromRight (120)); buttons.removeFromRight (8); cancelButton.setBounds (buttons.removeFromRight (80));
}

} // namespace beatmaker::ui
