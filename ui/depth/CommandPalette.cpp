#include "CommandPalette.h"

namespace beatmaker::ui
{

CommandPalette::CommandPalette (const CommandRegistry& r, const Preferences& p) : registry (r), prefs (p)
{
    addAndMakeVisible (input);
    input.setTextToShowWhenEmpty ("Type a command or setting...", theme::textDim);
    input.setFont (juce::FontOptions (16.0f));
    input.addListener (this);
    addAndMakeVisible (list);
    list.setModel (this);
    list.setRowHeight (30);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    setSize (preferredWidth, preferredHeight);
    refresh();
}

void CommandPalette::setQuery (const juce::String& q) { input.setText (q, juce::dontSendNotification); refresh(); }

void CommandPalette::refresh()
{
    rows.clear();
    const auto q = input.getText().trim();
    for (const auto* c : registry.search (q)) rows.push_back ({ c, nullptr });
    if (q.isNotEmpty())
        for (const auto* d : prefs.search (q)) rows.push_back ({ nullptr, d });
    list.updateContent();
    if (! rows.empty()) list.selectRow (0);
    list.repaint();
}

void CommandPalette::choose (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
    const auto r = rows[(size_t) row];
    if (onDismiss) onDismiss();
    if (r.command != nullptr) registry.run (r.command->id);
    else if (r.pref != nullptr && onOpenPreference) onOpenPreference (r.pref->id);
}

bool CommandPalette::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::downKey) { list.selectRow (juce::jmin (getNumRows() - 1, list.getSelectedRow() + 1)); return true; }
    if (key == juce::KeyPress::upKey)   { list.selectRow (juce::jmax (0, list.getSelectedRow() - 1)); return true; }
    if (key == juce::KeyPress::returnKey) { choose (list.getSelectedRow()); return true; }
    if (key == juce::KeyPress::escapeKey) { if (onDismiss) onDismiss(); return true; }
    return false;
}

void CommandPalette::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
    const auto& r = rows[(size_t) row];
    if (selected) { g.setColour (theme::accent.withAlpha (0.3f)); g.fillRect (0, 0, width, height); }
    g.setFont (juce::FontOptions (13.0f));
    if (r.command != nullptr)
    {
        const bool enabled = r.command->isEnabled();
        g.setColour (enabled ? theme::textDim : theme::textDim.withAlpha (0.5f));
        g.drawText (r.command->category + ":", 12, 0, 110, height, juce::Justification::centredLeft, true);
        g.setColour (enabled ? theme::text : theme::textDim);
        g.drawText (r.command->name, 124, 0, width - 260, height, juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        juce::String keys = r.command->shortcutText();
        if (r.command->focusKey != 0) keys += (keys.isEmpty() ? "" : "   ") + juce::String ("focus: ") + juce::String::charToString (r.command->focusKey).toUpperCase();
        g.drawText (keys, width - 140, 0, 130, height, juce::Justification::centredRight, true);
    }
    else if (r.pref != nullptr)
    {
        g.setColour (theme::accent);
        g.drawText ("Pref " + r.pref->category + ":", 12, 0, 110, height, juce::Justification::centredLeft, true);
        g.setColour (theme::text);
        g.drawText (r.pref->name, 124, 0, width - 260, height, juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.drawText (r.pref->type == PrefDef::Type::choice ? prefs.choiceName (r.pref->id) : prefs.get (r.pref->id).toString(), width - 140, 0, 130, height, juce::Justification::centredRight, true);
    }
}

void CommandPalette::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Up/Down to choose, Enter to run, Esc to close. Settings open in Preferences.", 12, getHeight() - 20, getWidth() - 24, 16, juce::Justification::centredLeft, true);
}

void CommandPalette::resized()
{
    auto area = getLocalBounds().reduced (10).withTrimmedBottom (18);
    input.setBounds (area.removeFromTop (34));
    area.removeFromTop (6);
    list.setBounds (area);
}

} // namespace beatmaker::ui
