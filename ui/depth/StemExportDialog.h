// StemExportDialog: bounce each track to its own file (Pro Tools "export mix sources").
#pragma once

#include "../shared/Theme.h"
#include <Session.h>
#include <bounce/Bouncer.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class StemExportDialog final : public juce::Component
{
public:
    struct Request
    {
        std::vector<int> tracks;
        bool includeAuxReturns = false, throughMasterInserts = false, cycleRange = false;
        engine::BounceSettings settings;   // format/bit depth/tail; range is filled by the owner
    };
    StemExportDialog (const model::Session&, bool hasCycle);
    std::function<void (const Request&)> onExport;
    std::function<void()> onCancel;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 460, preferredHeight = 460;

private:
    const model::Session& session;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<juce::ToggleButton> trackToggles;
    std::vector<int> trackIndices;
    juce::ToggleButton auxToggle { "Include aux returns in each stem" }, masterToggle { "Through the master's inserts" }, cycleToggle { "Cycle range only" };
    juce::ComboBox formatBox, depthBox;
    juce::Slider tailSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label formatLabel { {}, "File type" }, depthLabel { {}, "Bit depth" }, tailLabel { {}, "Tail" };
    juce::TextButton allButton { "All" }, noneButton { "None" }, exportButton { "Export Stems..." }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
