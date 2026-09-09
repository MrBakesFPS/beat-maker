#include "KitBuilder.h"

namespace beatmaker::ui
{

// A list row with a play button at its right
struct KitBuilder::Row final : juce::Component
{
    Row (KitBuilder& o, bool slotRow) : owner (o), isSlot (slotRow)
    {
        play.setTitle ("Play");
        play.setTooltip (isSlot ? "Play this slot of your kit" : "Play this pad");
        juce::Path shape; shape.addTriangle (0.0f, 0.0f, 10.0f, 5.0f, 0.0f, 10.0f);
        play.setShape (shape, false, true, false);
        play.setColours (theme::textDim, theme::text, theme::accent);
        play.setEnabled (owner.graph != nullptr);
        play.onClick = [this] { if (isSlot) owner.playSlot (row); else owner.playSource (row); };
        addAndMakeVisible (play);
    }
    void update (int rowIndex, const juce::String& primary, const juce::String& secondary, bool sel) { row = rowIndex; title = primary; subtitle = secondary; selected = sel; repaint(); }
    void paint (juce::Graphics& g) override
    {
        if (selected) { g.setColour (theme::accent.withAlpha (0.25f)); g.fillRect (getLocalBounds()); }
        const int right = getWidth() - 34;
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (12.5f, subtitle.isEmpty() ? juce::Font::plain : juce::Font::bold));
        g.drawText (title, 10, subtitle.isEmpty() ? 0 : 3, right - 10, subtitle.isEmpty() ? getHeight() : 16, juce::Justification::centredLeft, true);
        if (subtitle.isNotEmpty())
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (subtitle, 10, 19, right - 10, 14, juce::Justification::centredLeft, true);
        }
    }
    void resized() override { play.setBounds (getWidth() - 28, (getHeight() - 14) / 2, 14, 14); }
    void mouseDown (const juce::MouseEvent& e) override { if (auto* list = findParentComponentOfClass<juce::ListBox>()) list->selectRow (row, false, ! e.mods.isCtrlDown()); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (! isSlot) owner.useSelected(); }

    KitBuilder& owner;
    bool isSlot;
    int row = -1;
    juce::String title, subtitle;
    bool selected = false;
    juce::ShapeButton play { "Play", theme::textDim, theme::text, theme::accent };
};

struct KitBuilder::SlotList final : juce::ListBoxModel
{
    explicit SlotList (KitBuilder& o) : owner (o) {}
    int getNumRows() override { return engine::DrumKit::numPads; }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override
    {
        auto* r = dynamic_cast<Row*> (existing);
        if (r == nullptr) { delete existing; r = new Row (owner, true); }
        if (juce::isPositiveAndBelow (row, getNumRows()))
        {
            const auto& src = owner.def.pads[(size_t) row];
            const auto kit = owner.sourceKit (src.kit);
            const juce::String padName = kit != nullptr ? kit->pads[(size_t) juce::jlimit (0, engine::DrumKit::numPads - 1, src.pad)].name : juce::String ("?");
            static const char* roles[16] = { "Kick", "Snare", "Clap", "Rim", "Closed Hat", "Open Hat", "Low Tom", "Mid Tom", "High Tom", "Crash", "Ride", "Cowbell", "Shaker", "Clave", "Conga", "Sub" };
            r->update (row, juce::String (row + 1) + "  " + roles[row], padName + "   (" + src.kit + ")", selected);
        }
        return r;
    }
    void selectedRowsChanged (int) override { owner.useButton.setButtonText ("Use for slot " + juce::String (owner.slots.getSelectedRow() + 1)); }
    KitBuilder& owner;
};

struct KitBuilder::SourceList final : juce::ListBoxModel
{
    explicit SourceList (KitBuilder& o) : owner (o) {}
    int getNumRows() override { return owner.sourceKitName().isNotEmpty() ? engine::DrumKit::numPads : 0; }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override
    {
        auto* r = dynamic_cast<Row*> (existing);
        if (r == nullptr) { delete existing; r = new Row (owner, false); }
        if (juce::isPositiveAndBelow (row, getNumRows()))
        {
            const auto kit = owner.sourceKit (owner.sourceKitName());
            r->update (row, juce::String (row + 1) + "  " + (kit != nullptr ? kit->pads[(size_t) row].name : juce::String()), {}, selected);
        }
        return r;
    }
    void returnKeyPressed (int) override { owner.useSelected(); }
    KitBuilder& owner;
};

KitBuilder::KitBuilder (engine::AudioGraph* g, double sr, const juce::String& startFrom) : graph (g), sampleRate (sr)
{
    if (const auto* custom = engine::DrumKitFactory::customKit (startFrom)) { def = *custom; editing = true; }
    else
    {
        const auto* info = engine::DrumKitFactory::info (startFrom);
        const juce::String base = info != nullptr ? info->name : juce::String (engine::DrumKitFactory::defaultKitName());
        def.name = "My " + base;
        for (int i = 0; i < engine::DrumKit::numPads; ++i) def.pads[(size_t) i] = { base, i };
    }
    slotModel = std::make_unique<SlotList> (*this);
    sourceModel = std::make_unique<SourceList> (*this);
    for (auto* list : { &slots, &sourcePads }) { list->setColour (juce::ListBox::backgroundColourId, theme::background); addAndMakeVisible (*list); }
    slots.setModel (slotModel.get()); slots.setRowHeight (36);
    sourcePads.setModel (sourceModel.get()); sourcePads.setRowHeight (22);
    for (auto* label : { &slotsLabel, &sourceLabel, &nameLabel, &hint })
    {
        label->setColour (juce::Label::textColourId, theme::textDim);
        label->setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (*label);
    }
    addAndMakeVisible (sourceBox);
    rebuildSourceMenu();
    sourceBox.onChange = [this] { sourcePads.updateContent(); sourcePads.repaint(); };
    addAndMakeVisible (nameEditor);
    nameEditor.setText (def.name);
    useButton.setTooltip ("Put the selected pad of the source kit into the selected slot of your kit (a double-click on the pad does the same)");
    useButton.onClick = [this] { useSelected(); };
    wholeButton.setTooltip ("Fill every slot from the source kit, as a starting point");
    wholeButton.onClick = [this] { useWholeKit(); };
    saveButton.onClick = [this] { save (false); };
    saveAddButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.4f));
    saveAddButton.onClick = [this] { save (true); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    for (auto* b : { &useButton, &wholeButton, &saveButton, &saveAddButton, &cancelButton }) addAndMakeVisible (*b);
    slots.selectRow (0);
    sourcePads.updateContent();   // the source menu was filled without a change notification
    sourcePads.selectRow (0);
    setSize (preferredWidth, preferredHeight);
}

KitBuilder::~KitBuilder()
{
    if (graph != nullptr) graph->setAuditionKit (nullptr);
}

void KitBuilder::rebuildSourceMenu()
{
    sourceBox.clear (juce::dontSendNotification);
    sourceNames.clear();
    int id = 1;
    for (const auto& cat : engine::DrumKitFactory::categories())
    {
        sourceBox.addSectionHeading (cat);
        for (const auto& k : engine::DrumKitFactory::availableKits())
            if (k.category == cat && ! (editing && k.name == def.name))   // a kit is not built from itself
            { sourceBox.addItem (k.name, id++); sourceNames.push_back (k.name); }
    }
    if (! sourceNames.empty()) sourceBox.setSelectedId (1, juce::dontSendNotification);
}

juce::String KitBuilder::sourceKitName() const
{
    const int id = sourceBox.getSelectedId();
    return juce::isPositiveAndBelow (id - 1, (int) sourceNames.size()) ? sourceNames[(size_t) (id - 1)] : juce::String();
}

std::shared_ptr<const engine::DrumKit> KitBuilder::sourceKit (const juce::String& name)
{
    if (name.isEmpty()) return nullptr;
    auto& slot = sources[name];
    if (slot == nullptr) slot = engine::DrumKitFactory::createKit (name, sampleRate);
    return slot;
}

std::shared_ptr<const engine::DrumKit> KitBuilder::builtKit()
{
    if (builtDirty || built == nullptr)
    {
        auto kit = std::make_shared<engine::DrumKit>();
        kit->name = nameEditor.getText().trim();
        for (int i = 0; i < engine::DrumKit::numPads; ++i)
        {
            const auto src = sourceKit (def.pads[(size_t) i].kit);
            if (src != nullptr) kit->pads[(size_t) i] = src->pads[(size_t) juce::jlimit (0, engine::DrumKit::numPads - 1, def.pads[(size_t) i].pad)];
        }
        built = kit; builtDirty = false;
    }
    return built;
}

void KitBuilder::assign (int slot, const juce::String& kitName, int pad)
{
    if (! juce::isPositiveAndBelow (slot, engine::DrumKit::numPads) || kitName.isEmpty()) return;
    def.pads[(size_t) slot] = { kitName, juce::jlimit (0, engine::DrumKit::numPads - 1, pad) };
    builtDirty = true;
    slots.updateContent(); slots.repaint();
}

void KitBuilder::useSelected()
{
    assign (slots.getSelectedRow(), sourceKitName(), sourcePads.getSelectedRow());
    if (const int next = slots.getSelectedRow() + 1; next < engine::DrumKit::numPads) slots.selectRow (next);   // on to the next slot
}

void KitBuilder::useWholeKit()
{
    const auto name = sourceKitName();
    for (int i = 0; i < engine::DrumKit::numPads; ++i) assign (i, name, i);
}

void KitBuilder::playSlot (int slot)
{
    if (graph == nullptr) return;
    const auto kit = builtKit();
    if (loadedAudition != kit.get()) { graph->setAuditionKit (kit); loadedAudition = kit.get(); }
    graph->triggerAuditionPad (slot, 1.0f);
}

void KitBuilder::playSource (int pad)
{
    if (graph == nullptr) return;
    const auto kit = sourceKit (sourceKitName());
    if (kit == nullptr) return;
    if (loadedAudition != kit.get()) { graph->setAuditionKit (kit); loadedAudition = kit.get(); }
    graph->triggerAuditionPad (pad, 1.0f);
}

void KitBuilder::save (bool addTrack)
{
    def.name = nameEditor.getText().trim();
    if (def.name.isEmpty()) { hint.setText ("Give the kit a name", juce::dontSendNotification); return; }
    if (const auto* info = engine::DrumKitFactory::info (def.name); info != nullptr && ! info->custom) { hint.setText ("That is a bundled kit's name; pick another", juce::dontSendNotification); return; }
    if (onSave) onSave (def, addTrack);
}

void KitBuilder::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (editing ? "Edit Kit" : "Build Your Own Kit", 16, 10, getWidth() - 32, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Pick a slot on the left, a source kit and pad on the right, and press Use (or double-click the pad). Play buttons let you hear both sides.", 16, 28, getWidth() - 32, 16, juce::Justification::centredLeft, true);
}

void KitBuilder::resized()
{
    auto area = getLocalBounds().reduced (16).withTrimmedTop (36);
    auto buttons = area.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (80)); buttons.removeFromRight (6);
    saveAddButton.setBounds (buttons.removeFromRight (150)); buttons.removeFromRight (6);
    saveButton.setBounds (buttons.removeFromRight (90));
    hint.setBounds (buttons);
    area.removeFromBottom (8);
    auto nameRow = area.removeFromBottom (26);
    nameLabel.setBounds (nameRow.removeFromLeft (70));
    nameEditor.setBounds (nameRow.removeFromLeft (260));
    area.removeFromBottom (10);
    auto left = area.removeFromLeft (330); area.removeFromLeft (14);
    slotsLabel.setBounds (left.removeFromTop (16)); slots.setBounds (left);
    auto right = area;
    sourceLabel.setBounds (right.removeFromTop (16));
    sourceBox.setBounds (right.removeFromTop (24)); right.removeFromTop (6);
    auto useRow = right.removeFromBottom (26);
    useButton.setBounds (useRow.removeFromLeft (150)); useRow.removeFromLeft (8);
    wholeButton.setBounds (useRow.removeFromLeft (130));
    right.removeFromBottom (6);
    sourcePads.setBounds (right);
}

} // namespace beatmaker::ui
