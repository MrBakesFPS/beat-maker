#include "ShortcutsWindow.h"

namespace beatmaker::ui
{

ShortcutsWindow::ShortcutsWindow (const CommandRegistry& r) : registry (r)
{
    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Filter...", theme::textDim);
    search.addListener (this);
    addAndMakeVisible (list);
    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    setSize (preferredWidth, preferredHeight);
    refresh();
}

void ShortcutsWindow::refresh()
{
    rows.clear();
    const auto q = search.getText().trim();
    for (const auto* c : registry.search (q))
        if (c->shortcut.isValid() || c->focusKey != 0) rows.push_back (c);
    list.updateContent();
    list.repaint();
}

void ShortcutsWindow::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
    const auto& c = *rows[(size_t) row];
    if (row % 2 == 0) { g.setColour (theme::panel.withAlpha (0.4f)); g.fillRect (0, 0, width, height); }
    g.setFont (juce::FontOptions (12.5f));
    g.setColour (theme::textDim);
    g.drawText (c.category, 10, 0, 100, height, juce::Justification::centredLeft, true);
    g.setColour (theme::text);
    g.drawText (c.name, 112, 0, width - 300, height, juce::Justification::centredLeft, true);
    g.setColour (theme::accent);
    juce::String keys = c.shortcutText();
    if (c.focusKey != 0) keys += (keys.isEmpty() ? "" : "   ") + juce::String ("focus ") + juce::String::charToString (c.focusKey).toUpperCase();
    g.drawText (keys, width - 190, 0, 180, height, juce::Justification::centredRight, true);
}

void ShortcutsWindow::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Keyboard Shortcuts", 16, 10, 300, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("\"focus X\" keys apply in Commands Keyboard Focus (the a-z button). Everything else is always on.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void ShortcutsWindow::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (40);
    search.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);
    list.setBounds (area);
}

} // namespace beatmaker::ui
