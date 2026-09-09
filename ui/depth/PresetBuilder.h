// PresetBuilder: build your own instrument preset. Pick an instrument, start
// from any of its presets, turn its knobs (every parameter the instrument
// describes, a Wave menu where it has one), play the phrase to hear it, name
// it and save it as one of the user's presets (persistence/UserPresets).
#pragma once

#include "../shared/Theme.h"
#include <dsp/Instrument.h>
#include <graph/AudioGraph.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class PresetBuilder final : public juce::Component,
                            private juce::Timer
{
public:
    // Starts on `type` and its preset `startFrom` (empty = the instrument's usual starting preset); `initial`, when
    // given, are the parameters to start from instead (a track's current sound)
    PresetBuilder (engine::AudioGraph* graph, engine::InstrumentType type, const juce::String& startFrom, const engine::InstrumentParams* initial = nullptr);
    ~PresetBuilder() override;

    std::function<void (engine::InstrumentType, const engine::InstrumentParams&, bool addTrack)> onSave;
    std::function<void()> onCancel;

    engine::InstrumentType type() const noexcept { return currentType; }
    engine::InstrumentParams params() const;                  // the sound as it stands, named
    void setParam (int index, float value);
    void play();                                              // the phrase (C, E, G, then the chord)

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 760, preferredHeight = 520;

private:
    struct Knob { std::unique_ptr<juce::Slider> slider; std::unique_ptr<juce::Label> label; int param = -1; };
    void rebuildStartMenu();
    void rebuildKnobs();
    void loadPreset (const juce::String& name);
    void pushAudition (bool recreate);
    void timerCallback() override;
    void save (bool addTrack);

    engine::AudioGraph* graph = nullptr;
    engine::InstrumentType currentType = engine::InstrumentType::none;
    engine::InstrumentParams current;
    bool editingUser = false;                                 // started from a preset of the user's own: saving keeps its name
    bool auditionLoaded = false, paramsDirty = false;
    int phraseStep = -1;

    juce::ComboBox instrumentBox, startBox;
    std::vector<engine::InstrumentType> instrumentIds;
    std::vector<juce::String> startNames;
    std::unique_ptr<juce::ComboBox> waveBox; int waveParam = -1;
    juce::Label instrumentLabel { {}, "Instrument" }, startLabel { {}, "Start from" }, nameLabel { {}, "Preset name" }, waveLabel { {}, "Wave" }, hint;
    juce::TextEditor nameEditor;
    std::vector<Knob> knobs;
    juce::Component knobArea;
    juce::TextButton playButton { "Play" }, saveButton { "Save Preset" }, saveAddButton { "Save and Add Track" }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
