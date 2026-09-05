// EditToolbar: Pro Tools-style edit mode buttons (Shuffle/Slip/Spot/Grid),
// tool buttons (Zoomer/Trimmer/Selector/Grabber/Smart) and grid value.
#pragma once

#include "../shared/EditSettings.h"
#include "../shared/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatmaker::ui
{

class EditToolbar final : public juce::Component
{
public:
    explicit EditToolbar (EditSettings& settings);

    void refresh();          // sync buttons from settings
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 30;

private:
    EditSettings& settings;
    juce::OwnedArray<juce::TextButton> modeButtons, toolButtons;
    juce::ComboBox gridBox;
    juce::TextButton relativeButton { "Rel" };
    juce::Label hint;
};

} // namespace beatmaker::ui
