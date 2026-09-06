// MIDI synchronisation logic (pure, testable): generate MIDI Beat Clock and
// MIDI Time Code from the transport, and chase incoming clock or MTC.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>
#include <vector>

namespace beatmaker::engine
{

struct TransportSnapshot
{
    bool playing = false;
    double positionSeconds = 0.0;
    double bpm = 120.0;
};

struct TimedMidiMessage
{
    juce::MidiMessage message;
    double timeSeconds = 0.0;   // when it should go out (same clock as the snapshot's wall time)
};

enum class MtcFrameRate { fps24, fps25, fps2997drop, fps30 };
inline double mtcFramesPerSecond (MtcFrameRate r) { switch (r) { case MtcFrameRate::fps24: return 24.0; case MtcFrameRate::fps25: return 25.0; case MtcFrameRate::fps2997drop: return 29.97; case MtcFrameRate::fps30: return 30.0; } return 30.0; }
inline int mtcRateCode (MtcFrameRate r) { return (int) r; }   // 0=24, 1=25, 2=29.97df, 3=30 as in the MTC spec

struct Timecode
{
    int hours = 0, minutes = 0, seconds = 0, frames = 0;
    static Timecode fromSeconds (double s, MtcFrameRate rate);
    double toSeconds (MtcFrameRate rate) const;
    juce::String toString() const;
};

// MIDI Beat Clock: 24 ticks per beat, Start/Continue/Stop, Song Position Pointer on locate.
class MidiClockGenerator
{
public:
    void reset();
    // Call regularly (e.g. every 10 ms). `wallTime` is the time `now` was sampled.
    std::vector<TimedMidiMessage> update (const TransportSnapshot& now, double wallTime);
private:
    bool wasPlaying = false, started = false;
    double lastPosition = -1.0;
    long long lastTick = -1;
};

// MIDI Time Code: 8 quarter-frame messages per 2 frames while playing, a full-frame message on locate/stop.
class MtcGenerator
{
public:
    explicit MtcGenerator (MtcFrameRate rate = MtcFrameRate::fps30, double startOffsetSeconds = 0.0) : frameRate (rate), offset (startOffsetSeconds) {}
    void setFrameRate (MtcFrameRate r) { frameRate = r; reset(); }
    void setStartOffset (double seconds) { offset = seconds; reset(); }
    void reset();
    std::vector<TimedMidiMessage> update (const TransportSnapshot& now, double wallTime);
    static juce::MidiMessage fullFrame (const Timecode&, MtcFrameRate);
    static juce::MidiMessage quarterFrame (int piece, const Timecode&, MtcFrameRate);
private:
    MtcFrameRate frameRate;
    double offset;
    bool wasPlaying = false;
    double lastPosition = -1.0;
    long long lastQuarter = -1;
};

// Chasing incoming MIDI Beat Clock: tempo from tick spacing, start/stop/continue and song position.
class MidiClockChaser
{
public:
    struct Action { bool play = false, stop = false, cont = false; std::optional<double> locateBeats; std::optional<double> bpm; };
    Action handle (const juce::MidiMessage&, double timeSeconds);
    double getBpm() const noexcept { return bpm; }
    double getBeatPosition() const noexcept { return ticks / 24.0; }
    bool isRunning() const noexcept { return running; }
private:
    std::vector<double> tickTimes;
    long long ticks = 0;
    double bpm = 0.0;
    bool running = false;
};

// Chasing MTC: quarter frames assemble into a timecode (complete every 2 frames), full frames locate.
class MtcChaser
{
public:
    explicit MtcChaser (MtcFrameRate rate = MtcFrameRate::fps30) : frameRate (rate) {}
    struct Action { std::optional<double> positionSeconds; bool locate = false; bool running = false; };
    Action handle (const juce::MidiMessage&, double timeSeconds);
    MtcFrameRate getFrameRate() const noexcept { return frameRate; }
    // Running is assumed while quarter frames keep arriving; call to expire.
    bool isRunning (double nowSeconds) const noexcept { return lastQuarterTime >= 0.0 && nowSeconds - lastQuarterTime < 0.25; }
private:
    MtcFrameRate frameRate;
    int nibbles[8] {};
    int received = 0;
    double lastQuarterTime = -1.0;
};

} // namespace beatmaker::engine
