// FadesDialog: Pro Tools-style Fades window (Ctrl+F). Sets fade-in and
// fade-out length and shape plus clip gain for every selected clip (audio,
// MIDI or pattern: on the latter two the fades shape the instrument's sound).
#pragma once

#include "../shared/Theme.h"
#include <dsp/Fades.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class FadesDialog final : public juce::Component
{
public:
    struct Values
    {
        double fadeInMs = 0.0, fadeOutMs = 0.0;
        engine::FadeShape inShape = engine::FadeShape::linear, outShape = engine::FadeShape::linear;
        float gainDb = 0.0f;
    };

    FadesDialog (Values initial, int numClips);

    std::function<void (const Values&)> onApply;
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 400, preferredHeight = 260;

private:
    void drawCurve (juce::Graphics&, juce::Rectangle<float>, engine::FadeShape, bool fadeIn) const;
    Values current() const;

    int clips;
    juce::Label inLabel { {}, "Fade In" }, outLabel { {}, "Fade Out" }, gainLabel { {}, "Clip Gain" }, info;
    juce::Slider inSlider, outSlider, gainSlider;
    juce::ComboBox inShapeBox, outShapeBox;
    juce::TextButton applyButton { "Apply" }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
