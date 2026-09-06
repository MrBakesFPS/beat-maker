// EditToolbar: Pro Tools-style edit mode buttons (Shuffle/Slip/Spot/Grid),
// tool buttons (Zoomer/Trimmer/Selector/Grabber/Smart) and grid value.
#pragma once

#include "../shared/EditSettings.h"
#include "../shared/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatmaker::ui
{

class EditToolbar final : public juce::Component,
                          private juce::Timer
{
public:
    explicit EditToolbar (EditSettings& settings);

    void refresh();          // sync buttons from settings
    std::function<void()> onIOSetup;
    // Which editor has the keyboard: the note editor or the tracks. The app answers and switches.
    std::function<bool()> notesFocused;
    std::function<void (bool notes)> onFocusNotes;
    std::function<juce::String()> editorHint;   // the focused editor's key vocabulary
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 30;

private:
    EditSettings& settings;
    juce::OwnedArray<juce::TextButton> modeButtons, toolButtons;
    juce::ComboBox gridBox;
    juce::TextButton relativeButton { "Rel" };
    juce::TextButton tceButton { "TCE" };
    juce::TextButton focusButton { "a-z" };
    juce::TextButton notesButton { "Editor" };
    bool lastNotes = false;
    void timerCallback() override;
    juce::TextButton ioButton { "I/O..." };
    juce::Label hint;
};

} // namespace beatmaker::ui
