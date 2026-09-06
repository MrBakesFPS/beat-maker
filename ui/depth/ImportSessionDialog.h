// ImportSessionDialog: Pro Tools "Import Session Data": pick tracks from
// another session and how to place and merge them.
#pragma once

#include "../shared/Theme.h"
#include <SessionImport.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class ImportSessionDialog final : public juce::Component
{
public:
    ImportSessionDialog (const model::Session& source, const persistence::TransportState& sourceTransport, const juce::String& sourceName, double playheadSeconds);
    std::function<void (const persistence::ImportOptions&)> onImport;
    std::function<void()> onCancel;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 520, preferredHeight = 520;

private:
    const model::Session& source;
    persistence::TransportState transport;
    juce::String name;
    double playhead;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<juce::ToggleButton> trackToggles;
    juce::ComboBox placementBox, destinationBox;
    juce::Label placementLabel { {}, "Placement" }, destinationLabel { {}, "Destination" };
    juce::ToggleButton markersToggle { "Import memory locations and sections" }, tempoToggle { "Import tempo and meter" },
                       insertsToggle { "Inserts" }, sendsToggle { "Sends" }, automationToggle { "Automation" }, playlistsToggle { "Alternate playlists" };
    juce::TextButton allButton { "All" }, noneButton { "None" }, importButton { "Import" }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
