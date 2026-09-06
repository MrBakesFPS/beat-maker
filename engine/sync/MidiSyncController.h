// MidiSyncController: drives the generators/chasers from the transport on a
// message-thread timer and talks to real or virtual MIDI ports.
#pragma once

#include "MidiSync.h"
#include "../transport/Transport.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <memory>

namespace beatmaker::engine
{

class MidiSyncController final : private juce::Timer,
                                 private juce::MidiInputCallback
{
public:
    enum class Mode { off, sendClock, sendMtc, chaseClock, chaseMtc };
    static const char* modeName (Mode m)
    {
        switch (m) { case Mode::off: return "Off"; case Mode::sendClock: return "Send MIDI Beat Clock"; case Mode::sendMtc: return "Send MIDI Time Code";
                     case Mode::chaseClock: return "Chase MIDI Beat Clock"; case Mode::chaseMtc: return "Chase MIDI Time Code"; }
        return "";
    }
    static constexpr const char* virtualPortName = "Beat Maker Sync";

    explicit MidiSyncController (Transport&);
    ~MidiSyncController() override;

    void setMode (Mode);
    Mode getMode() const noexcept { return mode; }
    void setFrameRate (MtcFrameRate r) { frameRate = r; mtc.setFrameRate (r); mtcChaser = MtcChaser (r); }
    MtcFrameRate getFrameRate() const noexcept { return frameRate; }
    void setStartOffsetSeconds (double s) { startOffset = s; mtc.setStartOffset (s); }
    double getStartOffsetSeconds() const noexcept { return startOffset; }

    // Device names; "Beat Maker Sync" is a virtual port created on demand (ALSA/CoreMIDI).
    static juce::StringArray outputDeviceNames();
    static juce::StringArray inputDeviceNames();
    bool setOutputDevice (const juce::String& name);
    bool setInputDevice (const juce::String& name);
    juce::String getOutputDeviceName() const { return outputName; }
    juce::String getInputDeviceName() const { return inputName; }

    // Message thread readout for the Sync window / status bar.
    juce::String getStatus() const;
    long long getMessagesSent() const noexcept { return sent; }
    long long getMessagesReceived() const noexcept { return received.load(); }
    double getChasedBpm() const noexcept { return chasedBpm.load(); }

    // Chasing: called on the message thread when the external source moves the transport.
    std::function<void()> onTransportChanged;

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void send (const std::vector<TimedMidiMessage>&, double wallTime);

    Transport& transport;
    Mode mode = Mode::off;
    MtcFrameRate frameRate = MtcFrameRate::fps30;
    double startOffset = 0.0;
    MidiClockGenerator clock;
    MtcGenerator mtc;
    MidiClockChaser clockChaser;
    MtcChaser mtcChaser;
    juce::CriticalSection chaseLock;
    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<juce::MidiInput> input;
    juce::String outputName, inputName;
    long long sent = 0;
    std::atomic<long long> received { 0 };
    std::atomic<double> chasedBpm { 0.0 };
    double lastChaseTime = -1.0;
    juce::String lastTimecode;
};

} // namespace beatmaker::engine
