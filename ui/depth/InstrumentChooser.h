// InstrumentChooser: "Add Instrument Track > Other...": every instrument the
// program bundles, by category, with a description and its presets, so a
// track can start from the sound you want. Built from the instrument
// registry (engine::Instrument), so a new instrument shows up here by
// itself once it is registered.
#pragma once

#include "../shared/Theme.h"
#include <dsp/Instrument.h>
#include <graph/AudioGraph.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class InstrumentChooser final : public juce::Component,
                                private juce::Timer
{
public:
    // With a graph, choosing an instrument or preset plays a short phrase on it (the audition instrument)
    explicit InstrumentChooser (engine::AudioGraph* graph = nullptr);
    ~InstrumentChooser() override;

    // Preview: a short phrase (C, E, G, then the chord) on an instrument with a preset (empty = its starting preset).
    // Each row's play button calls this; playing another row or pressing the button again stops it.
    void playPreview (engine::InstrumentType, const juce::String& presetName);
    void stopPreview();
    bool isPreviewing (engine::InstrumentType, const juce::String& presetName) const;

    // The instrument and preset chosen (presetName empty = the instrument's usual starting preset)
    std::function<void (engine::InstrumentType, const juce::String& presetName)> onAdd;
    std::function<void()> onCancel;
    std::function<void (engine::InstrumentType, const juce::String& presetName)> onBuild;    // Build Your Own... / Edit... a preset
    std::function<void (engine::InstrumentType, const juce::String& presetName)> onRemove;   // Remove a preset of the user's own
    void refreshPresets();                                    // after the user's presets changed

    void selectType (engine::InstrumentType);
    engine::InstrumentType selectedType() const;
    juce::String selectedPreset() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 720, preferredHeight = 460;

private:
    struct CategoryList; struct InstrumentList; struct PresetList; struct Row;
    void rebuildInstruments();
    void rebuildPresets();
    void refreshButtons();
    void add();
    void timerCallback() override;
    void refreshPlayButtons();

    engine::AudioGraph* graph = nullptr;
    int phraseStep = -1;
    engine::InstrumentType previewType = engine::InstrumentType::none;   // what is playing (none = nothing)
    juce::String previewPreset;
    bool previewIsPreset = false;                          // started from a preset row (else an instrument row)

    std::vector<juce::String> categoryNames;                 // "All" first
    std::vector<engine::InstrumentType> shown;               // instruments of the chosen category
    std::vector<engine::InstrumentParams> presets;           // of the chosen instrument
    std::unique_ptr<CategoryList> categoryModel;
    std::unique_ptr<InstrumentList> instrumentModel;
    std::unique_ptr<PresetList> presetModel;
    juce::ListBox categories, instruments, presetBox;
    juce::Label categoryLabel { {}, "Category" }, instrumentLabel { {}, "Instrument" }, presetLabel { {}, "Start from" };
    juce::TextEditor description;
    juce::TextButton addButton { "Add Track" }, cancelButton { "Cancel" }, buildButton { "Build Your Own..." }, removeButton { "Remove" };
};

} // namespace beatmaker::ui
