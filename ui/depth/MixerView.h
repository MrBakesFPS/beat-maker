// MixerView: Pro Tools-style mix window. One channel strip per track plus a
// master strip: inserts, sends, pan, fader with meter, mute/solo, output.
#pragma once

#include "../shared/Theme.h"
#include <DelayCompensation.h>
#include <GroupLogic.h>
#include <MixerCommands.h>
#include <Session.h>
#include <graph/AudioGraph.h>
#include <metering/Loudness.h>

#include <juce_audio_processors/juce_audio_processors.h>
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
    std::function<void (int groupId)> onEditGroup;   // open the group dialog
    std::function<void (int trackIndex, int view)> onLaneViewChanged;   // Automation > Show Lane: 0 clips, 1 clip gain, 2 volume, 3 pan, 4 mute, 5 + send
    // Automation hooks (track index, parameter, value, gestureActive)
    std::function<void (int, const engine::ParamId&, float, bool)> onParameterChanged;
    std::function<void (int, const engine::ParamId&)> onGestureEnded;
    std::function<void (int, float trimDb, bool gestureActive)> onTrimChanged;   // Trim mode fader moves

    // Plugin hosting hooks
    std::function<juce::Array<juce::PluginDescription>()> knownPlugins;
    std::function<void (int trackIndex, int slot, const juce::PluginDescription&)> onInsertPlugin;
    std::function<void (int trackIndex, int slot)> onOpenPluginEditor;
    std::function<void()> onScanPlugins;
    std::function<void (int trackIndex, int slot)> onLoadImpulse;   // Convolution Reverb: pick an IR file
    std::function<std::optional<float> (int, const engine::ParamId&)> automatedValue;   // value to display when reading

    // Opens an insert's knobs by its slot on a track's strip, scrolling the strip into view (a track menu's Add Effect lands here)
    void openInsertEditor (int trackIndex, int slot);
    void openSendsPanel (int trackIndex);   // the strip's sends on a wide dB scale, a panel over the strips (toggles)
    void closeSendsPanel();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void visibilityChanged() override;

    static constexpr int preferredHeight = 440;

private:
    class ChannelStrip;
    void sessionChanged (model::Session&) override;
    void timerCallback() override;
    void rebuildStrips();
    void placeSendsPanel();

    model::Session& session;
    engine::AudioGraph& graph;
    std::function<double()> sampleRate;
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<ChannelStrip> masterStrip;
    std::unique_ptr<juce::Component> sendsPanel;   // the open sends panel, if any
    int sendsPanelTrack = -1;
    std::vector<model::StripDelayInfo> delays;

    engine::LoudnessAnalyser loudness;
    std::vector<engine::LoudnessBlock> loudnessScratch;
};

} // namespace beatmaker::ui
