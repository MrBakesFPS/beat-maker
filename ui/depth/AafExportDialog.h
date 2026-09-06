// AafExportDialog: options for File > Export AAF (tracks, embedded or linked
// audio, consolidated clips or whole files, bit depth, timecode rate, markers).
#pragma once

#include "../shared/Theme.h"
#include <AafExport.h>
#include <Session.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class AafExportDialog final : public juce::Component
{
public:
    explicit AafExportDialog (const model::Session&);
    std::function<void (const persistence::AafExportOptions&)> onExport;
    std::function<void()> onCancel;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 470, preferredHeight = 500;

private:
    const model::Session& session;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<juce::ToggleButton> trackToggles;
    std::vector<int> trackIndices;
    juce::ToggleButton embedToggle { "Embed audio in the AAF file" }, linkToggle { "Link to WAV files in a folder beside it" };
    juce::ToggleButton consolidateToggle { "Consolidate clips (gain, gain line and fades rendered)" }, wholeToggle { "Whole source files with handles (gain and fades not carried)" };
    juce::ToggleButton markersToggle { "Include markers" };
    juce::ComboBox depthBox, fpsBox;
    juce::Label depthLabel { {}, "Bit depth" }, fpsLabel { {}, "Timecode" };
    juce::TextButton allButton { "All" }, noneButton { "None" }, exportButton { "Export AAF..." }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
