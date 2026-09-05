#include "NewSessionDialog.h"

namespace beatmaker::ui
{

NewSessionDialog::NewSessionDialog (std::vector<Choice> c, juce::String defaultName) : choices (std::move (c))
{
    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    list.selectRow (0);
    addAndMakeVisible (list);
    addAndMakeVisible (nameLabel);
    nameLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (nameEditor);
    nameEditor.setText (defaultName);
    addAndMakeVisible (createButton);
    addAndMakeVisible (cancelButton);
    createButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    createButton.onClick = [this] { create(); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    setSize (preferredWidth, preferredHeight);
}

void NewSessionDialog::create()
{
    const int row = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, getNumRows())) return;
    if (onCreate) onCreate (choices[(size_t) row], nameEditor.getText().trim().isEmpty() ? "Untitled" : nameEditor.getText().trim());
}

void NewSessionDialog::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, getNumRows())) return;
    const auto& c = choices[(size_t) row];
    if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (0, 0, width, height); }
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (c.name, 10, 4, width - 20, 18, juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (c.description + (c.templateFile != juce::File() ? "   (your template)" : juce::String()), 10, 21, width - 20, 16, juce::Justification::centredLeft, true);
}

void NewSessionDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("New Session", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Pick a template. Save any session as a template with Open... > Save As Template.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void NewSessionDialog::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (36);
    auto buttons = area.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (80)); buttons.removeFromRight (6);
    createButton.setBounds (buttons.removeFromRight (100));
    area.removeFromBottom (8);
    auto nameRow = area.removeFromBottom (26);
    nameLabel.setBounds (nameRow.removeFromLeft (100));
    nameEditor.setBounds (nameRow);
    area.removeFromBottom (8);
    list.setBounds (area);
}

} // namespace beatmaker::ui
