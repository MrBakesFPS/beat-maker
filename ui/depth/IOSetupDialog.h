// IOSetupDialog: Pro Tools-style I/O Setup with Input / Output / Bus tabs.
// Edits a copy of the IOSetup; Apply issues one SetIOSetupCommand.
#pragma once

#include "../shared/Theme.h"
#include <IOSetup.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class IOSetupDialog final : public juce::Component
{
public:
    IOSetupDialog (model::IOSetup setup, int deviceInputs, int deviceOutputs, bool delayCompensation);

    std::function<void (const model::IOSetup&, bool delayCompensation)> onApply;
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 560, preferredHeight = 420;

private:
    class PathList;
    model::IOSetup setup;
    int deviceInputs, deviceOutputs;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::ToggleButton adcToggle { "Automatic Delay Compensation" };
    juce::TextButton applyButton { "Apply" }, cancelButton { "Cancel" };
    juce::Label info;
};

} // namespace beatmaker::ui
