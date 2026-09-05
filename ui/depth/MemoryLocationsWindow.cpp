#include "MemoryLocationsWindow.h"

namespace beatmaker::ui
{

MemoryLocationsWindow::MemoryLocationsWindow (model::Session& s) : session (s)
{
    session.addListener (this);
    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    addAndMakeVisible (list);
    addAndMakeVisible (addButton);
    addAndMakeVisible (deleteButton);
    addButton.setTooltip ("Add a memory location at the playhead (M)");
    addButton.onClick = [this] { if (onAdd) onAdd(); };
    deleteButton.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (juce::isPositiveAndBelow (row, getNumRows())) session.execute (std::make_unique<model::RemoveMarkerCommand> (session.getMarkers()[(size_t) row].id));
    };
    setSize (preferredWidth, preferredHeight);
}

MemoryLocationsWindow::~MemoryLocationsWindow() { session.removeListener (this); }

void MemoryLocationsWindow::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, getNumRows())) return;
    const auto& m = session.getMarkers()[(size_t) row];
    if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (0, 0, width, height); }
    g.setColour (m.colour);
    g.fillRoundedRectangle (6.0f, 5.0f, 6.0f, (float) height - 10.0f, 2.0f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String (m.id), 18, 0, 30, height, juce::Justification::centredLeft);
    g.drawText (m.name, 50, 0, width - 230, height, juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    const auto time = juce::String (m.seconds, 2) + " s" + (m.isRange() ? " - " + juce::String (m.endSeconds, 2) + " s" : juce::String());
    g.drawText (time, width - 180, 0, 110, height, juce::Justification::centredLeft);
    juce::String kind = m.isSection ? "Section" : "Marker";
    if (m.recallSelection) kind += " +sel";
    if (m.recallZoom) kind += " +zoom";
    g.drawText (kind, width - 70, 0, 66, height, juce::Justification::centredRight);
}

void MemoryLocationsWindow::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, getNumRows())) return;
    const int id = session.getMarkers()[(size_t) row].id;
    if (e.mods.isPopupMenu()) { if (onRename) onRename (id); return; }
    if (onRecall) onRecall (id);
}

void MemoryLocationsWindow::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, getNumRows()) && onRename) onRename (session.getMarkers()[(size_t) row].id);
}

void MemoryLocationsWindow::deleteKeyPressed (int row)
{
    if (juce::isPositiveAndBelow (row, getNumRows())) session.execute (std::make_unique<model::RemoveMarkerCommand> (session.getMarkers()[(size_t) row].id));
}

void MemoryLocationsWindow::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Click to recall, double-click to rename, Alt+1..9 recalls by number. M adds a marker, Shift+M a section from the selection.",
                8, getHeight() - 22, getWidth() - 16, 18, juce::Justification::centredLeft, true);
}

void MemoryLocationsWindow::resized()
{
    auto area = getLocalBounds().reduced (8).withTrimmedBottom (20);
    auto buttons = area.removeFromBottom (26);
    addButton.setBounds (buttons.removeFromLeft (120)); buttons.removeFromLeft (6);
    deleteButton.setBounds (buttons.removeFromLeft (80));
    area.removeFromBottom (6);
    list.setBounds (area);
}

} // namespace beatmaker::ui
