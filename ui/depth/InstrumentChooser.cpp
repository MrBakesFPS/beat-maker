#include "InstrumentChooser.h"

namespace beatmaker::ui
{

// The three lists share one look: a name, and for instruments a category tag
struct InstrumentChooser::CategoryList final : juce::ListBoxModel
{
    explicit CategoryList (InstrumentChooser& o) : owner (o) {}
    int getNumRows() override { return (int) owner.categoryNames.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, getNumRows())) return;
        if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (0, 0, width, height); }
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (owner.categoryNames[(size_t) row], 10, 0, width - 20, height, juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged (int) override { owner.rebuildInstruments(); }
    InstrumentChooser& owner;
};

struct InstrumentChooser::InstrumentList final : juce::ListBoxModel
{
    explicit InstrumentList (InstrumentChooser& o) : owner (o) {}
    int getNumRows() override { return (int) owner.shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, getNumRows())) return;
        const auto type = owner.shown[(size_t) row];
        if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (0, 0, width, height); }
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (engine::Instrument::typeName (type), 10, 4, width - 20, 18, juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (engine::Instrument::typeCategory (type)) + "   " + juce::String (engine::Instrument::presets (type).size()) + " presets",
                    10, 21, width - 20, 16, juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged (int) override { owner.rebuildPresets(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.add(); }
    void returnKeyPressed (int) override { owner.add(); }
    InstrumentChooser& owner;
};

struct InstrumentChooser::PresetList final : juce::ListBoxModel
{
    explicit PresetList (InstrumentChooser& o) : owner (o) {}
    int getNumRows() override { return (int) owner.presets.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, getNumRows())) return;
        if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (0, 0, width, height); }
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (owner.presets[(size_t) row].presetName, 10, 0, width - 20, height, juce::Justification::centredLeft, true);
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.add(); }
    void returnKeyPressed (int) override { owner.add(); }
    InstrumentChooser& owner;
};

InstrumentChooser::InstrumentChooser()
{
    categoryNames.push_back ("All");
    for (const auto& c : engine::Instrument::categories()) categoryNames.push_back (c);
    categoryModel = std::make_unique<CategoryList> (*this);
    instrumentModel = std::make_unique<InstrumentList> (*this);
    presetModel = std::make_unique<PresetList> (*this);
    for (auto* list : { &categories, &instruments, &presetBox })
    {
        list->setColour (juce::ListBox::backgroundColourId, theme::background);
        addAndMakeVisible (*list);
    }
    categories.setModel (categoryModel.get()); categories.setRowHeight (26);
    instruments.setModel (instrumentModel.get()); instruments.setRowHeight (40);
    presetBox.setModel (presetModel.get()); presetBox.setRowHeight (22);
    for (auto* label : { &categoryLabel, &instrumentLabel, &presetLabel })
    {
        label->setColour (juce::Label::textColourId, theme::textDim);
        label->setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (*label);
    }
    description.setMultiLine (true, true);
    description.setReadOnly (true);
    description.setScrollbarsShown (false);
    description.setCaretVisible (false);
    description.setColour (juce::TextEditor::backgroundColourId, theme::panelDark);
    description.setColour (juce::TextEditor::outlineColourId, theme::gridStrong);
    description.setColour (juce::TextEditor::textColourId, theme::text);
    description.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (description);
    addButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    addButton.onClick = [this] { add(); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (addButton);
    addAndMakeVisible (cancelButton);
    categories.selectRow (0);
    rebuildInstruments();
    setSize (preferredWidth, preferredHeight);
}

void InstrumentChooser::rebuildInstruments()
{
    const auto before = selectedType();
    const int row = categories.getSelectedRow();
    const juce::String category = juce::isPositiveAndBelow (row, (int) categoryNames.size()) && row > 0 ? categoryNames[(size_t) row] : juce::String();
    shown.clear();
    for (auto type : engine::Instrument::availableTypes())
        if (category.isEmpty() || category == engine::Instrument::typeCategory (type)) shown.push_back (type);
    instruments.updateContent();
    int keep = 0;
    for (int i = 0; i < (int) shown.size(); ++i) if (shown[(size_t) i] == before) keep = i;
    if (! shown.empty()) instruments.selectRow (keep);
    rebuildPresets();
}

void InstrumentChooser::rebuildPresets()
{
    const auto type = selectedType();
    presets = type == engine::InstrumentType::none ? std::vector<engine::InstrumentParams>{} : engine::Instrument::presets (type);
    presetBox.updateContent();
    // The Synth starts on its second preset (Pluck) when added from the menu; the chooser offers the same start
    if (! presets.empty()) presetBox.selectRow (type == engine::InstrumentType::subtractive && presets.size() > 1 ? 1 : 0);
    description.setText (type == engine::InstrumentType::none ? juce::String() : juce::String (engine::Instrument::typeDescription (type)), false);
    addButton.setEnabled (type != engine::InstrumentType::none);
    repaint();
}

void InstrumentChooser::selectType (engine::InstrumentType type)
{
    categories.selectRow (0);
    rebuildInstruments();
    for (int i = 0; i < (int) shown.size(); ++i) if (shown[(size_t) i] == type) { instruments.selectRow (i); break; }
}

engine::InstrumentType InstrumentChooser::selectedType() const
{
    const int row = instruments.getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) shown.size()) ? shown[(size_t) row] : engine::InstrumentType::none;
}

juce::String InstrumentChooser::selectedPreset() const
{
    const int row = presetBox.getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) presets.size()) ? presets[(size_t) row].presetName : juce::String();
}

void InstrumentChooser::add()
{
    const auto type = selectedType();
    if (type != engine::InstrumentType::none && onAdd) onAdd (type, selectedPreset());
}

void InstrumentChooser::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Add Instrument Track", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Pick an instrument and the preset it starts from. The Sound menu on the track changes the preset later.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void InstrumentChooser::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (36);
    auto buttons = area.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (80)); buttons.removeFromRight (6);
    addButton.setBounds (buttons.removeFromRight (110));
    area.removeFromBottom (10);
    auto left = area.removeFromLeft (130); area.removeFromLeft (10);
    categoryLabel.setBounds (left.removeFromTop (16)); categories.setBounds (left);
    auto middle = area.removeFromLeft (250); area.removeFromLeft (10);
    instrumentLabel.setBounds (middle.removeFromTop (16)); instruments.setBounds (middle);
    auto right = area;
    description.setBounds (right.removeFromTop (110));
    right.removeFromTop (10);
    presetLabel.setBounds (right.removeFromTop (16)); presetBox.setBounds (right);
}

} // namespace beatmaker::ui
