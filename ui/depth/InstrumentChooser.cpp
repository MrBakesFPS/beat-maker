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
    void selectedRowsChanged (int) override { if (owner.autoAudition.getToggleState()) owner.audition(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.add(); }
    void returnKeyPressed (int) override { owner.add(); }
    InstrumentChooser& owner;
};

InstrumentChooser::InstrumentChooser (engine::AudioGraph* g) : graph (g)
{
    autoAudition.setToggleState (true, juce::dontSendNotification);
    autoAudition.setColour (juce::ToggleButton::textColourId, theme::textDim);
    auditionButton.setTooltip ("Play a short phrase on the selected instrument and preset");
    auditionButton.onClick = [this] { audition(); };
    addAndMakeVisible (auditionButton);
    addAndMakeVisible (autoAudition);
    auditionButton.setEnabled (graph != nullptr); autoAudition.setEnabled (graph != nullptr);
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

InstrumentChooser::~InstrumentChooser()
{
    stopTimer();
    if (graph != nullptr) graph->setAuditionInstrument (nullptr, nullptr);   // the graph retires it; nothing plays on after the window
}

void InstrumentChooser::loadAudition()
{
    const auto type = selectedType(); const auto preset = selectedPreset();
    if (graph == nullptr || type == engine::InstrumentType::none) return;
    if (type == auditionType && preset == auditionPreset && graph->hasAuditionInstrument()) return;   // already loaded
    auditionType = type; auditionPreset = preset;
    auto params = engine::Instrument::defaultParams (type);
    for (const auto& p : presets) if (p.presetName == preset) params = p;
    if (type == engine::InstrumentType::sampler && params.sample == nullptr)
    {
        // The Sampler has nothing to play until a file is dropped on its track: a short sine stands in for one
        auto tone = std::make_shared<juce::AudioBuffer<float>> (1, 24000);
        for (int i = 0; i < 24000; ++i) tone->setSample (0, i, (float) (std::sin (juce::MathConstants<double>::twoPi * 261.63 * i / 48000.0) * (1.0 - i / 24000.0)));
        params.sample = tone; params.sampleRate = 48000.0; params.rootNote = 60; params.sampleName = "Preview tone";
    }
    graph->setAuditionInstrument (engine::Instrument::create (type, 48000.0), std::make_shared<const engine::InstrumentParams> (params));
}

void InstrumentChooser::audition()
{
    if (graph == nullptr) return;
    loadAudition();
    phraseStep = 0;
    timerCallback();   // the first note now, the rest on the timer
}

// The phrase: C4, E4, G4 one after the other, then the chord held (a monophonic instrument plays its last note)
void InstrumentChooser::timerCallback()
{
    if (graph == nullptr || phraseStep < 0) { stopTimer(); return; }
    static constexpr int notes[3] = { 60, 64, 67 };
    if (phraseStep < 3) { graph->triggerAuditionNote (notes[phraseStep], 0.85f, 0.26); ++phraseStep; startTimer (280); return; }
    if (phraseStep == 3) { for (int n : { 60, 64, 67, 72 }) graph->triggerAuditionNote (n, 0.8f, 1.1); ++phraseStep; startTimer (300); return; }
    phraseStep = -1; stopTimer();
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
    // Selecting the preset row plays it (PresetList::selectedRowsChanged) when Play on select is on
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
    auditionButton.setBounds (buttons.removeFromLeft (90)); buttons.removeFromLeft (8);
    autoAudition.setBounds (buttons.removeFromLeft (130));
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
