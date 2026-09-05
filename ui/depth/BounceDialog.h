// BounceDialog: Pro Tools-style "Bounce to Disk" settings. Collects range,
// format, bit depth, tail and normalisation; the owner picks the destination
// file and runs the render.
#pragma once

#include "../shared/Theme.h"
#include <bounce/Bouncer.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class BounceDialog final : public juce::Component
{
public:
    struct Context
    {
        double sampleRate = 44100.0;
        double arrangementSeconds = 0.0;
        double cycleStartSeconds = 0.0, cycleEndSeconds = 0.0;   // <= 0 length means no cycle
        int numOutputs = 2;
    };

    explicit BounceDialog (Context context);

    // Called with settings (start/end already resolved) when the user hits Bounce.
    std::function<void (const engine::BounceSettings&)> onBounce;
    std::function<void()> onCancel;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int preferredWidth = 420, preferredHeight = 340;

private:
    void updateBitDepths();
    void updateInfo();
    engine::BounceSettings buildSettings() const;

    Context ctx;

    juce::Label rangeLabel { {}, "Range" }, formatLabel { {}, "File type" }, depthLabel { {}, "Bit depth" },
                tailLabel { {}, "Tail" }, infoLabel;
    juce::ComboBox rangeBox, formatBox, depthBox;
    juce::Slider tailSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton normalizeToggle { "Normalize to -0.3 dBFS" };
    juce::ToggleButton trimToggle { "Trim trailing silence" };
    juce::TextButton bounceButton { "Bounce..." }, cancelButton { "Cancel" };
};

} // namespace beatmaker::ui
