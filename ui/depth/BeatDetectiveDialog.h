// BeatDetectiveDialog: Pro Tools-style Beat Detective window. Works on the
// selected clips (or the selected track): Separate at transients, Clip
// Conform to the edit grid, Edit Smoothing, or all three in a row.
#pragma once

#include "../shared/Theme.h"
#include "../shared/EditSettings.h"
#include <BeatDetective.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class BeatDetectiveDialog final : public juce::Component
{
public:
    explicit BeatDetectiveDialog (const EditSettings& edit);

    float sensitivity() const noexcept { return (float) sensitivitySlider.getValue() * 0.01f; }
    model::ConformSettings conformSettings (double bpm) const;
    model::SmoothingSettings smoothingSettings() const;

    std::function<void()> onSeparate, onConform, onSmooth, onAll;
    void setStatus (const juce::String&);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 420, preferredHeight = 380;

private:
    void addSlider (juce::Slider&, juce::Label&, double min, double max, double value, const juce::String& suffix);

    const EditSettings& edit;
    juce::Label sensitivityLabel { {}, "Sensitivity" }, strengthLabel { {}, "Strength" }, excludeLabel { {}, "Exclude Within" },
                swingLabel { {}, "Swing" }, crossfadeLabel { {}, "Crossfade" }, smoothingLabel { {}, "Smoothing" }, gridLabel;
    juce::Slider sensitivitySlider, strengthSlider, excludeSlider, swingSlider, crossfadeSlider;
    juce::ComboBox smoothingBox;
    juce::TextButton separateButton { "1. Separate" }, conformButton { "2. Conform" }, smoothButton { "3. Smooth" }, allButton { "Do All" };
    juce::Label status;
};

} // namespace beatmaker::ui
