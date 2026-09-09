#include "DrumKitChooser.h"

namespace beatmaker::ui
{

// A list row with a play/stop button at its right
struct DrumKitChooser::Row final : juce::Component
{
    Row (DrumKitChooser& o, bool padRow) : owner (o), isPad (padRow)
    {
        play.setTitle ("Preview");
        play.setTooltip (isPad ? "Play this pad" : "Play a bar of a beat on this kit (again to stop)");
        play.onClick = [this]
        {
            if (isPad) owner.playPad (row);
            else if (owner.isPlayingBeat (kitName)) owner.stopPreview();
            else owner.playBeat (kitName);
        };
        addAndMakeVisible (play);
    }
    void update (int rowIndex, const juce::String& kit, const juce::String& primary, const juce::String& secondary, bool sel)
    {
        row = rowIndex; kitName = kit; title = primary; subtitle = secondary; selected = sel;
        refreshButton(); repaint();
    }
    void refreshButton()
    {
        const bool playing = ! isPad && owner.isPlayingBeat (kitName);
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
    void mouseDown (const juce::MouseEvent& e) override { if (auto* list = findParentComponentOfClass<juce::ListBox>()) list->selectRow (row, false, ! e.mods.isCtrlDown()); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (! isPad) owner.add(); }

    DrumKitChooser& owner;
    bool isPad;
    int row = -1;
    juce::String kitName, title, subtitle;
    bool selected = false;
    juce::ShapeButton play { "Preview", theme::textDim, theme::text, theme::accent };
};

struct DrumKitChooser::CategoryList final : juce::ListBoxModel
{
    explicit CategoryList (DrumKitChooser& o) : owner (o) {}
    int getNumRows() override { return (int) owner.categoryNames.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, getNumRows())) return;
        if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (0, 0, width, height); }
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (owner.categoryNames[(size_t) row], 10, 0, width - 20, height, juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged (int) override { owner.rebuildKits(); }
    DrumKitChooser& owner;
};

struct DrumKitChooser::KitList final : juce::ListBoxModel
{
    explicit KitList (DrumKitChooser& o) : owner (o) {}
    int getNumRows() override { return (int) owner.shown.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override
    {
        auto* r = dynamic_cast<Row*> (existing);
        if (r == nullptr) { delete existing; r = new Row (owner, false); }
        if (juce::isPositiveAndBelow (row, getNumRows()))
        {
            const auto* k = owner.shown[(size_t) row];
            r->update (row, k->name, k->name, k->category + "   16 pads", selected);
        }
        return r;
    }
    void selectedRowsChanged (int) override { owner.rebuildPads(); }
    void returnKeyPressed (int) override { owner.add(); }
    DrumKitChooser& owner;
};

struct DrumKitChooser::PadList final : juce::ListBoxModel
{
    explicit PadList (DrumKitChooser& o) : owner (o) {}
    int getNumRows() override { return owner.auditionKit != nullptr || owner.selectedKit().isNotEmpty() ? engine::DrumKit::numPads : 0; }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override
    {
        auto* r = dynamic_cast<Row*> (existing);
        if (r == nullptr) { delete existing; r = new Row (owner, true); }
        if (juce::isPositiveAndBelow (row, getNumRows()))
        {
            const auto kitName = owner.selectedKit();
            const auto kit = owner.auditionKit != nullptr && owner.auditionName == kitName ? owner.auditionKit : owner.namesKit (kitName);
            r->update (row, kitName, juce::String (row + 1) + "  " + kit->pads[(size_t) row].name, {}, selected);
        }
        return r;
    }
    DrumKitChooser& owner;
};

DrumKitChooser::DrumKitChooser (engine::AudioGraph* g, double sr) : graph (g), sampleRate (sr)
{
    categoryNames.push_back ("All");
    for (const auto& c : engine::DrumKitFactory::categories()) categoryNames.push_back (c);
    categoryModel = std::make_unique<CategoryList> (*this);
    kitModel = std::make_unique<KitList> (*this);
    padModel = std::make_unique<PadList> (*this);
    for (auto* list : { &categories, &kits, &pads })
    {
        list->setColour (juce::ListBox::backgroundColourId, theme::background);
        addAndMakeVisible (*list);
    }
    categories.setModel (categoryModel.get()); categories.setRowHeight (26);
    kits.setModel (kitModel.get()); kits.setRowHeight (40);
    pads.setModel (padModel.get()); pads.setRowHeight (22);
    for (auto* label : { &categoryLabel, &kitLabel, &padLabel })
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
    buildButton.setTooltip ("Build a kit of your own from the pads of the other kits, starting from the selected one (a kit of yours is edited)");
    buildButton.onClick = [this] { if (onBuild) onBuild (selectedKit()); };
    removeButton.setTooltip ("Delete this kit of yours");
    removeButton.onClick = [this] { if (onRemove) onRemove (selectedKit()); };
    addAndMakeVisible (buildButton);
    addAndMakeVisible (removeButton);
    categories.selectRow (0);
    rebuildKits();
    setSize (preferredWidth, preferredHeight);
}

void DrumKitChooser::refreshKits()
{
    const auto keep = selectedKit();
    categoryNames.clear();
    categoryNames.push_back ("All");
    for (const auto& c : engine::DrumKitFactory::categories()) categoryNames.push_back (c);
    categories.updateContent();
    if (categories.getSelectedRow() < 0) categories.selectRow (0);
    rebuildKits();
    if (keep.isNotEmpty()) selectKit (keep);
}

DrumKitChooser::~DrumKitChooser()
{
    stopTimer();
    if (graph != nullptr) graph->setAuditionKit (nullptr);
}

std::shared_ptr<const engine::DrumKit> DrumKitChooser::namesKit (const juce::String& name)
{
    auto& slot = nameKits[name];
    if (slot == nullptr) slot = engine::DrumKitFactory::createKit (name, 8000.0);
    return slot;
}

void DrumKitChooser::rebuildKits()
{
    const auto before = selectedKit();
    const int row = categories.getSelectedRow();
    const juce::String category = juce::isPositiveAndBelow (row, (int) categoryNames.size()) && row > 0 ? categoryNames[(size_t) row] : juce::String();
    shown.clear();
    for (const auto& k : engine::DrumKitFactory::availableKits())
        if (category.isEmpty() || category == k.category) shown.push_back (&k);
    kits.updateContent();
    int keep = 0;
    for (int i = 0; i < (int) shown.size(); ++i) if (before == shown[(size_t) i]->name) keep = i;
    if (! shown.empty()) kits.selectRow (keep);
    rebuildPads();
}

void DrumKitChooser::rebuildPads()
{
    const auto name = selectedKit();
    const auto* info = engine::DrumKitFactory::info (name);
    description.setText (info != nullptr ? juce::String (info->description) : juce::String(), false);
    pads.updateContent();
    pads.repaint();
    addButton.setEnabled (info != nullptr);
    buildButton.setButtonText (info != nullptr && info->custom ? "Edit..." : "Build Your Own...");
    removeButton.setVisible (info != nullptr && info->custom);
    repaint();
}

void DrumKitChooser::selectKit (const juce::String& name)
{
    categories.selectRow (0);
    rebuildKits();
    for (int i = 0; i < (int) shown.size(); ++i) if (name == shown[(size_t) i]->name) { kits.selectRow (i); break; }
}

juce::String DrumKitChooser::selectedKit() const
{
    const int row = kits.getSelectedRow();
    return juce::isPositiveAndBelow (row, (int) shown.size()) ? shown[(size_t) row]->name : juce::String();
}

void DrumKitChooser::loadAudition (const juce::String& kitName)
{
    if (graph == nullptr) return;
    if (auditionKit != nullptr && auditionName == kitName && graph->hasAuditionInstrument()) return;
    auditionKit = engine::DrumKitFactory::createKit (kitName, sampleRate);
    auditionName = kitName;
    graph->setAuditionKit (auditionKit);
    pads.updateContent();
}

void DrumKitChooser::playPad (int pad)
{
    if (graph == nullptr) return;
    loadAudition (selectedKit());
    graph->triggerAuditionPad (pad, 1.0f);
}

void DrumKitChooser::playBeat (const juce::String& kitName)
{
    if (graph == nullptr || kitName.isEmpty()) return;
    stopPreview();
    loadAudition (kitName);
    beatKit = kitName; beatStep = 0;
    refreshPlayButtons();
    timerCallback();
}

void DrumKitChooser::stopPreview()
{
    stopTimer();
    if (beatStep < 0) return;
    beatStep = -1; beatKit.clear();
    if (graph != nullptr) graph->stopAuditionNotes();
    refreshPlayButtons();
}

void DrumKitChooser::refreshPlayButtons()
{
    for (int i = 0; i < kits.getNumRowsOnScreen() + 2; ++i)
        if (auto* r = dynamic_cast<Row*> (kits.getComponentForRowNumber (kits.getRowContainingPosition (0, 0) + i))) r->refreshButton();
}

// One bar of eighths at 120 BPM: kick and hat, hat, snare and hat, hat, kick and hat, kick and hat, snare and open hat, hat
void DrumKitChooser::timerCallback()
{
    using F = engine::DrumKitFactory;
    if (graph == nullptr || beatStep < 0) { stopTimer(); return; }
    static const std::vector<int> steps[8] = { { F::kick, F::closedHat }, { F::closedHat }, { F::snare, F::closedHat }, { F::closedHat },
                                               { F::kick, F::closedHat }, { F::kick, F::closedHat }, { F::snare, F::openHat }, { F::closedHat } };
    if (beatStep < 8)
    {
        for (int pad : steps[beatStep]) graph->triggerAuditionPad (pad, pad == F::closedHat ? 0.6f : 0.95f);
        ++beatStep; startTimer (250); return;
    }
    beatStep = -1; beatKit.clear(); stopTimer();
    refreshPlayButtons();
}

void DrumKitChooser::add()
{
    const auto name = selectedKit();
    if (name.isNotEmpty() && onAdd) onAdd (name);
}

void DrumKitChooser::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Add Drum Machine Track", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Pick a kit; the play button on a kit plays a bar of a beat on it, the one on a pad plays that pad. The Kit menu in the drum editor changes it later.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void DrumKitChooser::resized()
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
    kitLabel.setBounds (middle.removeFromTop (16)); kits.setBounds (middle);
    auto right = area;
    description.setBounds (right.removeFromTop (96));
    right.removeFromTop (10);
    padLabel.setBounds (right.removeFromTop (16)); pads.setBounds (right);
}

} // namespace beatmaker::ui
