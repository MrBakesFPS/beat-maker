// TutorialWindow: guided task lists that tick themselves off as you work.
#pragma once

#include "../shared/Theme.h"
#include "../shared/Tutorials.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class TutorialWindow final : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::Timer
{
public:
    explicit TutorialWindow (std::vector<Tutorial>);
    std::function<void (const juce::String& commandId)> runCommand;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 700, preferredHeight = 460;

private:
    class StepRow;
    void rebuildSteps();
    int getNumRows() override { return (int) tutorials.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override { rebuildSteps(); }
    void timerCallback() override;

    std::vector<Tutorial> tutorials;
    juce::ListBox list;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<StepRow> rows;
    juce::Label summary;
};

} // namespace beatmaker::ui
