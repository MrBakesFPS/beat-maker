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

    void audition();                                       // play the phrase on the selected instrument and preset

    // The instrument and preset chosen (presetName empty = the instrument's usual starting preset)
    std::function<void (engine::InstrumentType, const juce::String& presetName)> onAdd;
    std::function<void()> onCancel;

    void selectType (engine::InstrumentType);
    engine::InstrumentType selectedType() const;
    juce::String selectedPreset() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 720, preferredHeight = 460;

private:
    struct CategoryList; struct InstrumentList; struct PresetList;
    void rebuildInstruments();
    void rebuildPresets();
    void add();
    void timerCallback() override;
    void loadAudition();                                   // hand the selected instrument and preset to the graph

    engine::AudioGraph* graph = nullptr;
    int phraseStep = -1;
    engine::InstrumentType auditionType = engine::InstrumentType::none;
    juce::String auditionPreset;

    std::vector<juce::String> categoryNames;                 // "All" first
    std::vector<engine::InstrumentType> shown;               // instruments of the chosen category
    std::vector<engine::InstrumentParams> presets;           // of the chosen instrument
    std::unique_ptr<CategoryList> categoryModel;
    std::unique_ptr<InstrumentList> instrumentModel;
    std::unique_ptr<PresetList> presetModel;
    juce::ListBox categories, instruments, presetBox;
    juce::Label categoryLabel { {}, "Category" }, instrumentLabel { {}, "Instrument" }, presetLabel { {}, "Start from" };
    juce::TextEditor description;
    juce::TextButton addButton { "Add Track" }, cancelButton { "Cancel" }, auditionButton { "Audition" };
    juce::ToggleButton autoAudition { "Play on select" };
};

} // namespace beatmaker::ui
