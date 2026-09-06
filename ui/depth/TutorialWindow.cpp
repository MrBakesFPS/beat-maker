#include "TutorialWindow.h"

namespace beatmaker::ui
{

class TutorialWindow::StepRow final : public juce::Component
{
public:
    StepRow (const TutorialStep& s, int number, std::function<void()> showMe) : step (s), index (number)
    {
        if (step.commandId.isNotEmpty())
        {
            button.setButtonText ("Do it");
            button.setTooltip ("Run the command for this step");
            button.onClick = std::move (showMe);
            addAndMakeVisible (button);
        }
    }
    void refresh() { const bool now = step.isDone && step.isDone(); if (now != done) { done = now; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::gridStrong);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        const juce::Rectangle<float> box (10.0f, 10.0f, 18.0f, 18.0f);
        g.setColour (done ? theme::accent : theme::gridStrong);
        g.drawRoundedRectangle (box, 4.0f, 1.5f);
        if (done)
        {
            juce::Path tick; tick.startNewSubPath (14.0f, 19.0f); tick.lineTo (18.0f, 23.0f); tick.lineTo (25.0f, 14.0f);
            g.strokePath (tick, juce::PathStrokeType (2.2f));
        }
        g.setColour (done ? theme::textDim : theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1) + ".  " + step.title, 38, 6, getWidth() - 140, 20, juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (step.hint, 38, 26, getWidth() - 140, 30, juce::Justification::topLeft, 2);
    }
    void resized() override { button.setBounds (getWidth() - 86, 12, 70, 24); }
private:
    TutorialStep step;
    int index;
    bool done = false;
    juce::TextButton button;
};

TutorialWindow::TutorialWindow (std::vector<Tutorial> t) : tutorials (std::move (t))
{
    addAndMakeVisible (list);
    list.setModel (this);
    list.setRowHeight (44);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (summary);
    summary.setColour (juce::Label::textColourId, theme::textDim);
    summary.setFont (juce::FontOptions (12.0f));
    setSize (preferredWidth, preferredHeight);
    list.selectRow (0);
    rebuildSteps();
    startTimerHz (4);
}

void TutorialWindow::rebuildSteps()
{
    rows.clear();
    holder.removeAllChildren();
    const int sel = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (sel, (int) tutorials.size())) return;
    const auto& tut = tutorials[(size_t) sel];
    int y = 0;
    for (int i = 0; i < (int) tut.steps.size(); ++i)
    {
        const auto id = tut.steps[(size_t) i].commandId;
        auto* row = rows.add (new StepRow (tut.steps[(size_t) i], i, [this, id] { if (runCommand) runCommand (id); }));
        row->setBounds (0, y, juce::jmax (200, viewport.getWidth() - 12), 62);
        holder.addAndMakeVisible (row);
        y += 62;
    }
    holder.setSize (juce::jmax (200, viewport.getWidth() - 12), y);
    summary.setText (tut.summary, juce::dontSendNotification);
    timerCallback();
}

void TutorialWindow::timerCallback()
{
    for (auto* r : rows) r->refresh();
    list.repaint();
}

void TutorialWindow::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) tutorials.size())) return;
    const auto& t = tutorials[(size_t) row];
    if (selected) { g.setColour (theme::accent.withAlpha (0.3f)); g.fillRect (0, 0, width, height); }
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (t.title, 10, 4, width - 20, 18, juce::Justification::centredLeft, true);
    g.setColour (t.isComplete() ? theme::accent : theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (t.isComplete() ? "Complete" : juce::String (t.doneCount()) + " / " + juce::String (t.steps.size()) + " steps", 10, 22, width - 20, 16, juce::Justification::centredLeft);
}

void TutorialWindow::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Tutorials", 16, 10, 200, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Steps tick themselves as you do them in the session; \"Do it\" performs a step for you.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void TutorialWindow::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (40);
    list.setBounds (area.removeFromLeft (220));
    area.removeFromLeft (10);
    summary.setBounds (area.removeFromTop (36));
    area.removeFromTop (4);
    viewport.setBounds (area);
    rebuildSteps();
}

} // namespace beatmaker::ui
