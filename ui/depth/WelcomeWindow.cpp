#include "WelcomeWindow.h"

namespace beatmaker::ui
{

WelcomeWindow::WelcomeWindow (juce::StringArray names, juce::StringArray descs, bool showAtStartup) : samples (std::move (names)), descriptions (std::move (descs))
{
    for (auto* b : { &newButton, &openButton, &tourButton, &tutorialsButton, &shortcutsButton, &closeButton }) addAndMakeVisible (b);
    newButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    newButton.onClick = [this] { if (onNewSession) onNewSession(); };
    openButton.onClick = [this] { if (onOpenSession) onOpenSession(); };
    tourButton.onClick = [this] { if (onTour) onTour(); };
    tutorialsButton.onClick = [this] { if (onTutorials) onTutorials(); };
    shortcutsButton.onClick = [this] { if (onShortcuts) onShortcuts(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    for (int i = 0; i < samples.size(); ++i)
    {
        auto* b = sampleButtons.add (new juce::TextButton (samples[i]));
        b->setTooltip (descriptions[i]);
        b->onClick = [this, i] { if (onOpenSample) onOpenSample (samples[i]); };
        addAndMakeVisible (b);
    }
    addAndMakeVisible (startupToggle);
    startupToggle.setToggleState (showAtStartup, juce::dontSendNotification);
    startupToggle.onClick = [this] { if (onShowAtStartupChanged) onShowAtStartupChanged (startupToggle.getToggleState()); };
    setSize (preferredWidth, preferredHeight);
}

void WelcomeWindow::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Welcome to Beat Maker", 24, 18, getWidth() - 48, 30, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("A GarageBand-style surface with Pro Tools-style depth. Start with a template, open a finished sample project, or take the two-minute tour.",
                24, 50, getWidth() - 48, 34, juce::Justification::topLeft, true);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SAMPLE PROJECTS", 24, 196, 200, 16, juce::Justification::centredLeft);
    g.drawText ("LEARN", 300, 196, 200, 16, juce::Justification::centredLeft);
}

void WelcomeWindow::resized()
{
    auto area = getLocalBounds().reduced (24).withTrimmedTop (70);
    auto top = area.removeFromTop (30);
    newButton.setBounds (top.removeFromLeft (250)); top.removeFromLeft (10);
    openButton.setBounds (top.removeFromLeft (170));
    area.removeFromTop (74);
    auto columns = area.removeFromTop (110);
    auto left = columns.removeFromLeft (250), right = columns.withTrimmedLeft (26);
    for (auto* b : sampleButtons) { b->setBounds (left.removeFromTop (28)); left.removeFromTop (6); }
    tourButton.setBounds (right.removeFromTop (28)); right.removeFromTop (6);
    tutorialsButton.setBounds (right.removeFromTop (28)); right.removeFromTop (6);
    shortcutsButton.setBounds (right.removeFromTop (28));
    auto bottom = getLocalBounds().reduced (24).removeFromBottom (28);
    closeButton.setBounds (bottom.removeFromRight (110));
    startupToggle.setBounds (bottom.removeFromLeft (240));
}

} // namespace beatmaker::ui
