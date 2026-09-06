// SyncDialog: Pro Tools Session Setup-style synchronisation: send or chase
// MIDI Beat Clock / MIDI Time Code, frame rate, session start time, ports.
#pragma once

#include "../shared/Theme.h"
#include <sync/MidiSyncController.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class SyncDialog final : public juce::Component,
                         private juce::Timer
{
public:
    explicit SyncDialog (engine::MidiSyncController&);
    std::function<void()> onChanged;   // the owner persists the settings
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredWidth = 520, preferredHeight = 300;

private:
    void timerCallback() override { status.setText (sync.getStatus(), juce::dontSendNotification); }
    void apply();
    engine::MidiSyncController& sync;
    juce::Label modeLabel { {}, "Sync mode" }, rateLabel { {}, "MTC frame rate" }, startLabel { {}, "Session start" }, outLabel { {}, "MIDI output" }, inLabel { {}, "MIDI input" }, status;
    juce::ComboBox modeBox, rateBox, outBox, inBox;
    juce::TextEditor startEditor;
};

} // namespace beatmaker::ui
