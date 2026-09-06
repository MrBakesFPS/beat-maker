// WelcomeWindow: first-launch entry points: templates, sample projects, the tour, tutorials, shortcuts.
#pragma once

#include "../shared/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class WelcomeWindow final : public juce::Component
{
public:
    WelcomeWindow (juce::StringArray sampleNames, juce::StringArray sampleDescriptions, bool showAtStartup);
    std::function<void()> onNewSession, onOpenSession, onTour, onTutorials, onShortcuts, onClose;
    std::function<void (const juce::String&)> onOpenSample;
    std::function<void (bool)> onShowAtStartupChanged;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 560, preferredHeight = 400;

private:
    juce::StringArray samples, descriptions;
    juce::TextButton newButton { "New Session from Template..." }, openButton { "Open a Session..." }, tourButton { "Take the Tour" },
                     tutorialsButton { "Tutorials" }, shortcutsButton { "Keyboard Shortcuts" }, closeButton { "Start Empty" };
    juce::OwnedArray<juce::TextButton> sampleButtons;
    juce::ToggleButton startupToggle { "Show this window at startup" };
};

} // namespace beatmaker::ui
