// SmartControls: GarageBand-style macro knobs for the selected track.
// Every track gets Volume and Pan; instrument tracks add one knob per
// parameter the instrument describes (Instrument::paramInfo); drum tracks
// add pad-group levels.
// A knob drag is one undo step: intermediate values replace the previous
// command in the history.
#pragma once

#include "../shared/Theme.h"
#include <Session.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class SmartControls final : public juce::Component,
                            private model::Session::Listener
{
public:
    explicit SmartControls (model::Session& session);
    ~SmartControls() override;

    void setTrack (int trackIndex);
    int getTrackIndex() const noexcept { return trackIndex; }

    // (command, replacePrevious): replacePrevious is true for the second and
    // later values of one knob gesture so the whole drag undoes at once.
    std::function<void (std::unique_ptr<model::Command>, bool replacePrevious)> onCommand;
    std::function<void (int, const engine::ParamId&, float, bool)> onParameterChanged;
    std::function<void (int, const engine::ParamId&)> onGestureEnded;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 128;
    static constexpr int titleWidth = 150;

private:
    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::function<void (double)> apply;   // builds and issues the command
    };

    void sessionChanged (model::Session&) override;
    void rebuild();
    void syncValues();
    void addKnob (const juce::String& name, double min, double max, double value, double skewMidpoint,
                  const juce::String& suffix, std::function<juce::String (double)> textFromValue,
                  std::function<void (double)> apply);
    void issue (std::unique_ptr<model::Command> cmd);

    // Bindings
    void bindMix (const model::Track&);
    void bindInstrument (const model::Track&);
    void bindDrums (const model::Track&);

    model::Session& session;
    int trackIndex = -1;
    std::vector<Knob> knobs;
    std::unique_ptr<juce::ComboBox> waveBox;     // instruments with a "Wave" parameter get a menu instead of a knob
    juce::Label waveLabel { {}, "Wave" };
    int waveParamIndex = -1;
    std::vector<int> knobParamIndices;           // instrument knob i -> InstrumentParams::values index
    engine::InstrumentType boundType = engine::InstrumentType::none;
    bool gestureActive = false, gestureChanged = false, syncing = false;
};

} // namespace beatmaker::ui
