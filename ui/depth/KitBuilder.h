// KitBuilder: build your own drum kit from the pads of the other kits. Sixteen
// slots on the left, a source kit and its pads on the right; a pad goes into
// the selected slot with Use (or a double-click), every slot has a play
// button, and the result is saved as one of the user's kits (persistence/UserKits).
#pragma once

#include "../shared/Theme.h"
#include <dsp/DrumKitFactory.h>
#include <graph/AudioGraph.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>

namespace beatmaker::ui
{

class KitBuilder final : public juce::Component
{
public:
    // Starts from `startFrom`: a custom kit is edited, any other kit fills the slots with its own pads
    KitBuilder (engine::AudioGraph* graph, double sampleRate, const juce::String& startFrom);
    ~KitBuilder() override;

    std::function<void (const engine::DrumKitFactory::CustomKit&, bool addTrack)> onSave;
    std::function<void()> onCancel;

    const engine::DrumKitFactory::CustomKit& definition() const noexcept { return def; }
    void assign (int slot, const juce::String& sourceKit, int sourcePad);
    void setName (const juce::String& name) { nameEditor.setText (name); }

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 760, preferredHeight = 560;

private:
    struct SlotList; struct SourceList; struct Row;
    std::shared_ptr<const engine::DrumKit> sourceKit (const juce::String& name);
    std::shared_ptr<const engine::DrumKit> builtKit();
    juce::String sourceKitName() const;
    void playSlot (int slot);
    void playSource (int pad);
    void useSelected();
    void useWholeKit();
    void rebuildSourceMenu();
    void save (bool addTrack);

    engine::AudioGraph* graph = nullptr;
    double sampleRate = 48000.0;
    engine::DrumKitFactory::CustomKit def;
    bool editing = false;                                   // an existing custom kit: its name may stay
    std::map<juce::String, std::shared_ptr<const engine::DrumKit>> sources;
    std::shared_ptr<const engine::DrumKit> built;
    bool builtDirty = true;
    const engine::DrumKit* loadedAudition = nullptr;

    std::unique_ptr<SlotList> slotModel;
    std::unique_ptr<SourceList> sourceModel;
    juce::ListBox slots, sourcePads;
    juce::ComboBox sourceBox;
    std::vector<juce::String> sourceNames;
    juce::Label slotsLabel { {}, "Your kit" }, sourceLabel { {}, "Take pads from" }, nameLabel { {}, "Kit name" }, hint;
    juce::TextEditor nameEditor;
    juce::TextButton useButton { "Use for slot" }, wholeButton { "Use whole kit" }, saveButton { "Save Kit" }, saveAddButton { "Save and Add Track" }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
