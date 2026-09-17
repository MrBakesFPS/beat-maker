// GarageBand-style transport strip bound to the engine Transport.
#pragma once

#include "../shared/Theme.h"
#include <transport/Transport.h>

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportBar (engine::Transport& transport);

    std::function<void()> onOpenFile;
    std::function<void()> onRecord;
    std::function<void()> onBounce;
    std::function<void (bool)> onEditorToggled;
    std::function<void (bool)> onMetronomeToggled;
    void setMetronome (bool on) { clickButton.setToggleState (on, juce::dontSendNotification); }
    std::function<void (bool)> onControlsToggled;
    std::function<void (bool)> onMixerToggled;
    std::function<void (juce::TextButton&)> onRecordModeClicked;   // main shows the mode menu
    std::function<void (juce::TextButton&)> onRollClicked;         // main shows the pre/post-roll menu
    void setRecordModeText (const juce::String& t) { recordModeButton.setButtonText (t); }
    void setRollText (const juce::String& t) { rollButton.setButtonText (t); }
    // QuickPunch/TrackPunch: rolling with inputs captured but not punched in (record button shows amber).
    void setWaitingForPunch (bool waiting) { waitingForPunch = waiting; }
    // CPU readout (0..1) and overrun count, shown at the right of the LCDs
    void setCpuLoad (float load, long long overruns) { cpuLoad = load; cpuOverruns = overruns; cpuLabel.repaint(); }
    std::function<void()> onCpuClicked;
    // Tempo LCD: double-click to type, drag up/down or wheel to change, right-click for the time signature.
    std::function<void (double bpm, int beatsPerBar)> onTempoEdited;   // commit (one undo step)
    std::function<void (double bpm)> onTempoPreview;                    // live while dragging
    void setControlsVisible (bool visible) { controlsButton.setToggleState (visible, juce::dontSendNotification); }
    void setMixerVisible (bool visible) { mixerButton.setToggleState (visible, juce::dontSendNotification); }

    void setEditorVisible (bool visible)  { editorButton.setToggleState (visible, juce::dontSendNotification); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void updateDisplay();

    engine::Transport& transport;
    bool blinkOn = false, waitingForPunch = false;
    int blinkCounter = 0;

    juce::ShapeButton recordButton { "Record", theme::record, theme::record.brighter(), theme::record.darker() };
    juce::ShapeButton playButton   { "Play",   theme::play,   theme::play.brighter(),   theme::play.darker() };
    juce::ShapeButton stopButton   { "Stop",   theme::stop,   theme::stop.brighter(),   theme::stop.darker() };
    juce::ShapeButton rtzButton    { "Return to Start", theme::stop, theme::stop.brighter(), theme::stop.darker() };

    juce::Label barsBeatsLcd, timeLcd, tempoLcd;
    juce::Label cpuLabel;
    float cpuLoad = 0.0f; long long cpuOverruns = 0;
    double dragStartBpm = 0.0, previewBpm = 0.0; bool draggingTempo = false, tempoPreviewing = false;
    int wheelCommitTicks = 0;
    void showTimeSignatureMenu();
    juce::TextButton controlsButton { "Controls" };
    juce::TextButton mixerButton { "Mix" };
    juce::TextButton cycleButton { "Cycle" };
    juce::TextButton clickButton { "Click" };
    juce::TextButton recordModeButton { "Rec: Normal" };
    juce::TextButton rollButton { "Pre/Post" };
    juce::TextButton editorButton { "Editor" };
    juce::TextButton openButton { "Open..." };
    juce::TextButton bounceButton { "Bounce..." };
};

} // namespace beatmaker::ui
