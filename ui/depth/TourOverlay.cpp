#include "TourOverlay.h"

namespace beatmaker::ui
{

TourOverlay::TourOverlay()
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (true);
    for (auto* b : { &backButton, &nextButton, &skipButton }) addAndMakeVisible (b);
    nextButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    backButton.onClick = [this] { if (index > 0) show (index - 1); };
    nextButton.onClick = [this] { if (index + 1 < (int) steps.size()) show (index + 1); else finish(); };
    skipButton.onClick = [this] { finish(); };
    setVisible (false);
}

void TourOverlay::start (std::vector<Step> newSteps)
{
    steps = std::move (newSteps);
    if (steps.empty()) return;
    setVisible (true);
    toFront (true);
    show (0);
}

void TourOverlay::finish()
{
    setVisible (false);
    if (onFinished) onFinished();
}

void TourOverlay::show (int step)
{
    index = juce::jlimit (0, (int) steps.size() - 1, step);
    if (steps[(size_t) index].before) steps[(size_t) index].before();
    backButton.setEnabled (index > 0);
    nextButton.setButtonText (index + 1 < (int) steps.size() ? "Next  (" + juce::String (index + 1) + "/" + juce::String (steps.size()) + ")" : "Done");
    resized();
    repaint();
    grabKeyboardFocus();
}

juce::Rectangle<int> TourOverlay::targetBounds() const
{
    if (index < (int) steps.size() && steps[(size_t) index].target)
        if (auto* c = steps[(size_t) index].target(); c != nullptr && getParentComponent() != nullptr && c->isShowing())
            return getLocalArea (c, c->getLocalBounds()).expanded (6);
    return getLocalBounds().reduced (getWidth() / 3, getHeight() / 3);
}

void TourOverlay::resized()
{
    const auto target = targetBounds();
    const int w = 360, h = 150;
    // Prefer below the target, else above, else to the right; keep inside the window
    juce::Rectangle<int> box (target.getX(), target.getBottom() + 12, w, h);
    if (box.getBottom() > getHeight()) box.setY (target.getY() - h - 12);
    if (box.getY() < 0) box = { target.getRight() + 12, juce::jmax (0, target.getY()), w, h };
    box.setX (juce::jlimit (8, juce::jmax (8, getWidth() - w - 8), box.getX()));
    box.setY (juce::jlimit (8, juce::jmax (8, getHeight() - h - 8), box.getY()));
    callout = box;
    auto buttons = box.reduced (14).removeFromBottom (26);
    skipButton.setBounds (buttons.removeFromLeft (90));
    nextButton.setBounds (buttons.removeFromRight (110)); buttons.removeFromRight (6);
    backButton.setBounds (buttons.removeFromRight (70));
}

void TourOverlay::paint (juce::Graphics& g)
{
    const auto target = targetBounds();
    g.setColour (juce::Colours::black.withAlpha (0.62f));
    juce::Path shade;
    shade.addRectangle (getLocalBounds().toFloat());
    shade.addRoundedRectangle (target.toFloat(), 6.0f);
    shade.setUsingNonZeroWinding (false);
    g.fillPath (shade);
    g.setColour (theme::accent);
    g.drawRoundedRectangle (target.toFloat(), 6.0f, 2.0f);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (callout.toFloat(), 8.0f);
    g.setColour (theme::accent);
    g.drawRoundedRectangle (callout.toFloat(), 8.0f, 1.5f);
    if (index < (int) steps.size())
    {
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (steps[(size_t) index].title, callout.reduced (14).removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (steps[(size_t) index].text, callout.reduced (14).withTrimmedTop (26).withTrimmedBottom (32), juce::Justification::topLeft, 4);
    }
}

bool TourOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { finish(); return true; }
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::rightKey || key == juce::KeyPress::spaceKey) { nextButton.triggerClick(); return true; }
    if (key == juce::KeyPress::leftKey) { backButton.triggerClick(); return true; }
    return true;   // the tour owns the keyboard while it runs
}

} // namespace beatmaker::ui
