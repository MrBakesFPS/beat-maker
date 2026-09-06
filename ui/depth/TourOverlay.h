// TourOverlay: a spotlight walkthrough over the main window: each step
// highlights one component and explains it, with Next / Back / Skip.
#pragma once

#include "../shared/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace beatmaker::ui
{

class TourOverlay final : public juce::Component
{
public:
    struct Step
    {
        juce::String title, text;
        std::function<juce::Component*()> target;   // null = whole window
        std::function<void()> before;               // e.g. open the mixer
    };
    TourOverlay();
    void start (std::vector<Step> steps);
    void finish();
    bool isRunning() const noexcept { return isVisible(); }
    std::function<void()> onFinished;
    int getStepIndex() const noexcept { return index; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override {}   // swallow clicks outside the callout

private:
    void show (int step);
    juce::Rectangle<int> targetBounds() const;
    std::vector<Step> steps;
    int index = 0;
    juce::TextButton backButton { "Back" }, nextButton { "Next" }, skipButton { "Skip tour" };
    juce::Rectangle<int> callout;
};

} // namespace beatmaker::ui
