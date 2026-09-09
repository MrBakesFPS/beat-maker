#include "InstrumentChooser.h"

namespace beatmaker::ui
{

// A list row with a play/stop button at its right: the button previews the row's sound without selecting the row
struct InstrumentChooser::Row final : juce::Component
{
    Row (InstrumentChooser& o, bool preset) : owner (o), isPreset (preset)
    {
        play.setTitle ("Preview");
        play.setTooltip ("Play a short phrase on this sound (again to stop)");
        play.onClick = [this]
        {
            if (owner.isPreviewing (type, presetName)) owner.stopPreview();
            else owner.playPreview (type, presetName);
        };
        addAndMakeVisible (play);
    }
    void update (int rowIndex, engine::InstrumentType t, const juce::String& preset, const juce::String& primary, const juce::String& secondary, bool sel)
    {
        row = rowIndex; type = t; presetName = preset; title = primary; subtitle = secondary; selected = sel;
        refreshButton();
        repaint();
    }
    void refreshButton()
    {
        const bool playing = owner.isPreviewing (type, presetName);
        juce::Path shape;
        if (playing) shape.addRectangle (0.0f, 0.0f, 10.0f, 10.0f);
        else shape.addTriangle (0.0f, 0.0f, 10.0f, 5.0f, 0.0f, 10.0f);
        play.setShape (shape, false, true, false);
        const auto c = playing ? theme::accent : theme::textDim;
        play.setColours (c, c.brighter (0.3f), theme::accent);
        play.setEnabled (owner.graph != nullptr);
    }
    void paint (juce::Graphics& g) override
    {
        if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (getLocalBounds()); }
        const int right = getWidth() - 34;
        g.setColour (theme::text);
        if (subtitle.isEmpty())
        {
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (title, 10, 0, right - 10, getHeight(), juce::Justification::centredLeft, true);
        }
        else
        {
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (title, 10, 4, right - 10, 18, juce::Justification::centredLeft, true);
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (subtitle, 10, 21, right - 10, 16, juce::Justification::centredLeft, true);
        }
    }
    void resized() override { play.setBounds (getWidth() - 28, (getHeight() - 14) / 2, 14, 14); }
    // Clicks on the row itself select it (the ListBox handles the click when we pass it on)
    void mouseDown (const juce::MouseEvent& e) override { if (auto* list = findParentComponentOfClass<juce::ListBox>()) list->selectRow (row, false, ! e.mods.isCtrlDown()); }
    void mouseDoubleClick (const juce::MouseEvent&) override { owner.add(); }

    InstrumentChooser& owner;
    bool isPreset;
    int row = -1;
    engine::InstrumentType type = engine::InstrumentType::none;
    juce::String presetName, title, subtitle;
    bool selected = false;
    juce::ShapeButton play { "Preview", theme::textDim, theme::text, theme::accent };
};

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
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override
    {
        auto* r = dynamic_cast<Row*> (existing);
        if (r == nullptr) { delete existing; r = new Row (owner, false); }
        if (juce::isPositiveAndBelow (row, getNumRows()))
        {
            const auto type = owner.shown[(size_t) row];
            r->update (row, type, {}, engine::Instrument::typeName (type),
                       juce::String (engine::Instrument::typeCategory (type)) + "   " + juce::String (engine::Instrument::presets (type).size()) + " presets", selected);
        }
        return r;
    }
    void selectedRowsChanged (int) override { owner.rebuildPresets(); }
    void returnKeyPressed (int) override { owner.add(); }
    InstrumentChooser& owner;
};

struct InstrumentChooser::PresetList final : juce::ListBoxModel
{
    explicit PresetList (InstrumentChooser& o) : owner (o) {}
    int getNumRows() override { return (int) owner.presets.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override
    {
        auto* r = dynamic_cast<Row*> (existing);
        if (r == nullptr) { delete existing; r = new Row (owner, true); }
        if (juce::isPositiveAndBelow (row, getNumRows()))
        {
            const auto& name = owner.presets[(size_t) row].presetName;
            r->update (row, owner.selectedType(), name, name + (engine::Instrument::isUserPreset (owner.selectedType(), name) ? "   (yours)" : juce::String()), {}, selected);
        }
        return r;
    }
    void selectedRowsChanged (int) override { owner.refreshButtons(); }
    void returnKeyPressed (int) override { owner.add(); }
    InstrumentChooser& owner;
};

InstrumentChooser::InstrumentChooser (engine::AudioGraph* g) : graph (g)
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
    buildButton.setTooltip ("Build a preset of your own, starting from the selected one (a preset of yours is edited)");
    buildButton.onClick = [this] { if (onBuild) onBuild (selectedType(), selectedPreset()); };
    removeButton.setTooltip ("Delete this preset of yours");
    removeButton.onClick = [this] { if (onRemove) onRemove (selectedType(), selectedPreset()); };
    addAndMakeVisible (buildButton);
    addAndMakeVisible (removeButton);
    categories.selectRow (0);
    rebuildInstruments();
    setSize (preferredWidth, preferredHeight);
}

void InstrumentChooser::refreshPresets()
{
    const auto keepType = selectedType(); const auto keepPreset = selectedPreset();
    rebuildPresets();
    for (int i = 0; i < (int) presets.size(); ++i) if (presets[(size_t) i].presetName == keepPreset) presetBox.selectRow (i);
    juce::ignoreUnused (keepType);
    instruments.repaint();   // preset counts
}

void InstrumentChooser::refreshButtons()
{
    const bool user = engine::Instrument::isUserPreset (selectedType(), selectedPreset());
    buildButton.setButtonText (user ? "Edit..." : "Build Your Own...");
    removeButton.setVisible (user);
}

InstrumentChooser::~InstrumentChooser()
{
    stopTimer();
    if (graph != nullptr) graph->setAuditionInstrument (nullptr, nullptr);   // the graph retires it; nothing plays on after the window
}

bool InstrumentChooser::isPreviewing (engine::InstrumentType type, const juce::String& preset) const
{
    return previewType != engine::InstrumentType::none && previewType == type && previewPreset == preset;
}

void InstrumentChooser::playPreview (engine::InstrumentType type, const juce::String& preset)
{
    if (graph == nullptr || type == engine::InstrumentType::none) return;
    stopPreview();
    auto params = engine::Instrument::defaultParams (type);
    const auto all = engine::Instrument::presets (type);
    // An instrument's own button plays the preset a new track would start from; a preset's button plays that preset
    size_t index = type == engine::InstrumentType::subtractive && all.size() > 1 ? 1 : 0;
    for (size_t i = 0; i < all.size(); ++i) if (preset.isNotEmpty() && all[i].presetName == preset) index = i;
    if (! all.empty()) params = all[index];
    if (type == engine::InstrumentType::sampler && params.sample == nullptr)
    {
        // The Sampler has nothing to play until a file is dropped on its track: a short sine stands in for one
        auto tone = std::make_shared<juce::AudioBuffer<float>> (1, 24000);
        for (int i = 0; i < 24000; ++i) tone->setSample (0, i, (float) (std::sin (juce::MathConstants<double>::twoPi * 261.63 * i / 48000.0) * (1.0 - i / 24000.0)));
        params.sample = tone; params.sampleRate = 48000.0; params.rootNote = 60; params.sampleName = "Preview tone";
    }
    graph->setAuditionInstrument (engine::Instrument::create (type, 48000.0), std::make_shared<const engine::InstrumentParams> (params));
    previewType = type; previewPreset = preset; previewIsPreset = preset.isNotEmpty();
    phraseStep = 0;
    refreshPlayButtons();
    timerCallback();   // the first note now, the rest on the timer
}

void InstrumentChooser::stopPreview()
{
    stopTimer();
    phraseStep = -1;
    if (previewType == engine::InstrumentType::none) return;
    if (graph != nullptr) graph->stopAuditionNotes();
    previewType = engine::InstrumentType::none; previewPreset.clear();
    refreshPlayButtons();
}

void InstrumentChooser::refreshPlayButtons()
{
    for (auto* list : { &instruments, &presetBox })
        for (int i = 0; i < list->getNumRowsOnScreen() + 2; ++i)
            if (auto* r = dynamic_cast<Row*> (list->getComponentForRowNumber (list->getRowContainingPosition (0, 0) + i))) r->refreshButton();
}

// The phrase: C4, E4, G4 one after the other, then the chord held (a monophonic instrument plays its last note).
// When it ends the button turns back into a play button.
void InstrumentChooser::timerCallback()
{
    if (graph == nullptr || phraseStep < 0) { stopTimer(); return; }
    static constexpr int notes[3] = { 60, 64, 67 };
    if (phraseStep < 3) { graph->triggerAuditionNote (notes[phraseStep], 0.85f, 0.26); ++phraseStep; startTimer (280); return; }
    if (phraseStep == 3) { for (int n : { 60, 64, 67, 72 }) graph->triggerAuditionNote (n, 0.8f, 1.1); ++phraseStep; startTimer (1400); return; }
    phraseStep = -1; stopTimer();
    previewType = engine::InstrumentType::none; previewPreset.clear();   // the notes have released by themselves
    refreshPlayButtons();
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
    refreshButtons();
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
    g.drawText ("Pick an instrument and the preset it starts from; the play button on a row previews it. The Sound menu on the track changes the preset later.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void InstrumentChooser::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (36);
    auto buttons = area.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (80)); buttons.removeFromRight (6);
    addButton.setBounds (buttons.removeFromRight (110));
    buildButton.setBounds (buttons.removeFromLeft (130)); buttons.removeFromLeft (8);
    removeButton.setBounds (buttons.removeFromLeft (80));
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
