#include "ImportSessionDialog.h"
#include <ClipEdits.h>

namespace beatmaker::ui
{

ImportSessionDialog::ImportSessionDialog (const model::Session& s, const persistence::TransportState& t, const juce::String& n, double playheadSeconds)
    : source (s), transport (t), name (n), playhead (playheadSeconds)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    int y = 0;
    for (int i = 0; i < source.getNumTracks(); ++i)
    {
        const auto& tr = source.getTracks()[(size_t) i];
        const int clips = model::ClipEdits::numClips (tr, model::ClipEdits::kindForTrack (tr));
        const juce::String kind = tr.isDrumMachine() ? "Drum Machine" : tr.isSynth() ? juce::String (engine::Instrument::typeName (tr.instrumentType())) : tr.isAux() ? "Aux" : tr.isVca() ? "VCA" : "Audio";
        auto* toggle = trackToggles.add (new juce::ToggleButton (juce::String (i + 1) + "  " + tr.name + "   (" + kind + ", " + juce::String (clips) + (clips == 1 ? " clip" : " clips") + ")"));
        toggle->setToggleState (true, juce::dontSendNotification);
        toggle->setBounds (4, y, 440, 24);
        holder.addAndMakeVisible (toggle);
        y += 24;
    }
    holder.setSize (460, juce::jmax (24, y));

    for (auto* l : { &placementLabel, &destinationLabel }) { l->setColour (juce::Label::textColourId, theme::textDim); addAndMakeVisible (l); }
    addAndMakeVisible (placementBox);
    placementBox.addItem ("Maintain absolute time", 1);
    placementBox.addItem ("Start at the playhead (" + juce::String (playhead, 2) + " s)", 2);
    placementBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (destinationBox);
    destinationBox.addItem ("New tracks", 1);
    destinationBox.addItem ("Existing tracks with the same name (clips appended)", 2);
    destinationBox.setSelectedId (1, juce::dontSendNotification);
    for (auto* t2 : { &markersToggle, &tempoToggle, &insertsToggle, &sendsToggle, &automationToggle, &playlistsToggle }) addAndMakeVisible (t2);
    markersToggle.setToggleState (! source.getMarkers().empty(), juce::dontSendNotification);
    tempoToggle.setButtonText ("Import tempo and meter (" + juce::String (transport.bpm, 1) + " BPM, " + juce::String (transport.beatsPerBar) + "/4)");
    for (auto* t2 : { &insertsToggle, &sendsToggle, &automationToggle, &playlistsToggle }) t2->setToggleState (true, juce::dontSendNotification);
    for (auto* b : { &allButton, &noneButton, &importButton, &cancelButton }) addAndMakeVisible (b);
    allButton.onClick = [this] { for (auto* tg : trackToggles) tg->setToggleState (true, juce::dontSendNotification); };
    noneButton.onClick = [this] { for (auto* tg : trackToggles) tg->setToggleState (false, juce::dontSendNotification); };
    importButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    importButton.onClick = [this]
    {
        persistence::ImportOptions o;
        for (int i = 0; i < trackToggles.size(); ++i) if (trackToggles[i]->getToggleState()) o.tracks.push_back (i);
        o.offsetSeconds = placementBox.getSelectedId() == 2 ? playhead : 0.0;
        o.destination = destinationBox.getSelectedId() == 2 ? persistence::ImportOptions::Destination::matchByName : persistence::ImportOptions::Destination::newTracks;
        o.importMarkers = markersToggle.getToggleState();
        o.importTempo = tempoToggle.getToggleState();
        o.includeInserts = insertsToggle.getToggleState(); o.includeSends = sendsToggle.getToggleState();
        o.includeAutomation = automationToggle.getToggleState(); o.includePlaylists = playlistsToggle.getToggleState();
        if (onImport) onImport (o);
    };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    setSize (preferredWidth, preferredHeight);
}

void ImportSessionDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Import Session Data from \"" + name + "\"", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (source.getNumTracks()) + " tracks, " + juce::String (source.getMarkers().size()) + " memory locations. Audio stays referenced where it is.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
    g.drawText ("Include:", 16, getHeight() - 92, 60, 22, juce::Justification::centredLeft);
}

void ImportSessionDialog::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (40);
    auto buttons = area.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (80)); buttons.removeFromRight (6);
    importButton.setBounds (buttons.removeFromRight (100));
    allButton.setBounds (buttons.removeFromLeft (50)); buttons.removeFromLeft (4); noneButton.setBounds (buttons.removeFromLeft (60));
    area.removeFromBottom (8);
    auto include = area.removeFromBottom (22);
    include.removeFromLeft (60);
    for (auto* t2 : { &insertsToggle, &sendsToggle, &automationToggle, &playlistsToggle }) t2->setBounds (include.removeFromLeft (t2 == &playlistsToggle ? 150 : 95));
    area.removeFromBottom (4);
    tempoToggle.setBounds (area.removeFromBottom (22));
    markersToggle.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (6);
    auto row = area.removeFromBottom (26); destinationLabel.setBounds (row.removeFromLeft (90)); destinationBox.setBounds (row);
    area.removeFromBottom (4);
    row = area.removeFromBottom (26); placementLabel.setBounds (row.removeFromLeft (90)); placementBox.setBounds (row);
    area.removeFromBottom (8);
    viewport.setBounds (area);
}

} // namespace beatmaker::ui
