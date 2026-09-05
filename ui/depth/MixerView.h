// MixerView: Pro Tools-style mix window. One channel strip per track plus a
// master strip: inserts, sends, pan, fader with meter, mute/solo, output.
#pragma once

#include "../shared/Theme.h"
#include <DelayCompensation.h>
#include <MixerCommands.h>
#include <Session.h>
#include <graph/AudioGraph.h>
#include <metering/Loudness.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class MixerView final : public juce::Component,
                        private model::Session::Listener,
                        private juce::Timer
{
public:
    MixerView (model::Session& session, engine::AudioGraph& graph, std::function<double()> getSampleRate);
    ~MixerView() override;

    // (command, replacePrevious) - replacePrevious coalesces a fader gesture into one undo step
    std::function<void (std::unique_ptr<model::Command>, bool replacePrevious)> onCommand;
    std::function<void (int trackIndex)> onSelectTrack;
    // Automation hooks (track index, parameter, value, gestureActive)
    std::function<void (int, const engine::ParamId&, float, bool)> onParameterChanged;
    std::function<void (int, const engine::ParamId&)> onGestureEnded;
    std::function<std::optional<float> (int, const engine::ParamId&)> automatedValue;   // value to display when reading

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredHeight = 440;

private:
    class ChannelStrip;
    void sessionChanged (model::Session&) override;
    void timerCallback() override;
    void rebuildStrips();

    model::Session& session;
    engine::AudioGraph& graph;
    std::function<double()> sampleRate;
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
    std::vector<model::StripDelayInfo> delays;

    engine::LoudnessAnalyser loudness;
    std::vector<engine::LoudnessBlock> loudnessScratch;
};

} // namespace beatmaker::ui
