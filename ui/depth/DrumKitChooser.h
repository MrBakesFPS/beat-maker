// DrumKitChooser: "Add Drum Machine Track > Other...": every bundled kit by
// category with a description and its sixteen pads; a play button on a kit row
// plays a bar of a beat on it, one on a pad row plays that pad. Built from
// DrumKitFactory, so a new kit shows up here by itself.
#pragma once

#include "../shared/Theme.h"
#include <dsp/DrumKitFactory.h>
#include <graph/AudioGraph.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class DrumKitChooser final : public juce::Component,
                             private juce::Timer
{
public:
    // With a graph, the play buttons preview kits and pads (the audition kit); the sample rate is for synthesising them
    DrumKitChooser (engine::AudioGraph* graph = nullptr, double sampleRate = 48000.0);
    ~DrumKitChooser() override;

    std::function<void (const juce::String& kitName)> onAdd;
    std::function<void()> onCancel;

    void selectKit (const juce::String& name);
    juce::String selectedKit() const;

    void playBeat (const juce::String& kitName);   // a bar of kick, hats and snare on the kit (again to stop)
    void playPad (int pad);                        // one pad of the selected kit
    void stopPreview();
    bool isPlayingBeat (const juce::String& kitName) const { return beatStep >= 0 && beatKit == kitName; }

    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 720, preferredHeight = 480;

private:
    struct CategoryList; struct KitList; struct PadList; struct Row;
    void rebuildKits();
    void rebuildPads();
    void add();
    void loadAudition (const juce::String& kitName);
    void refreshPlayButtons();
    void timerCallback() override;

    engine::AudioGraph* graph = nullptr;
    double sampleRate = 48000.0;
    std::vector<juce::String> categoryNames;      // "All" first
    std::vector<const engine::DrumKitFactory::KitInfo*> shown;
    std::shared_ptr<const engine::DrumKit> auditionKit;   // the kit loaded into the graph's audition slot
    juce::String auditionName, beatKit;
    int beatStep = -1;
    std::unique_ptr<CategoryList> categoryModel;
    std::unique_ptr<KitList> kitModel;
    std::unique_ptr<PadList> padModel;
    juce::ListBox categories, kits, pads;
    juce::Label categoryLabel { {}, "Category" }, kitLabel { {}, "Kit" }, padLabel { {}, "Pads" };
    juce::TextEditor description;
    juce::TextButton addButton { "Add Track" }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
