// Transport: play/stop state and the timeline position, shared between the
// message thread (UI) and the audio thread. Everything here is atomic and
// lock-free so the audio thread can read it without blocking.
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>

namespace beatmaker::engine
{

struct BarBeat
{
    int bar  = 1;   // 1-based
    int beat = 1;   // 1-based
    int tick = 0;   // 0..959 (960 ticks per beat, matching Pro Tools)
};

class Transport
{
public:
    static constexpr int ticksPerBeat = 960;

    // ---- Playback state (any thread) ----
    void play() noexcept                  { playing.store (true,  std::memory_order_release); }
    void stop() noexcept                  { playing.store (false, std::memory_order_release); }
    void togglePlay() noexcept            { playing.store (! isPlaying(), std::memory_order_release); }
    bool isPlaying() const noexcept       { return playing.load (std::memory_order_acquire); }

    void returnToStart() noexcept         { position.store (0, std::memory_order_release); }

    // ---- Position ----
    juce::int64 getPositionSamples() const noexcept { return position.load (std::memory_order_acquire); }
    void setPositionSamples (juce::int64 samples) noexcept
    {
        position.store (juce::jmax<juce::int64> (0, samples), std::memory_order_release);
    }

    double getPositionSeconds() const noexcept
    {
        return static_cast<double> (getPositionSamples()) / getSampleRate();
    }

    void setPositionSeconds (double seconds) noexcept
    {
        setPositionSamples (static_cast<juce::int64> (std::llround (seconds * getSampleRate())));
    }

    // Called by the audio thread once per block.
    void advance (int numSamples) noexcept
    {
        position.fetch_add (numSamples, std::memory_order_acq_rel);
    }

    // ---- Cycle / loop range ----
    void setLoopEnabled (bool on) noexcept  { loopEnabled.store (on, std::memory_order_release); }
    bool isLoopEnabled() const noexcept     { return loopEnabled.load (std::memory_order_acquire); }
    void setLoopRange (juce::int64 startSample, juce::int64 endSample) noexcept
    {
        loopStart.store (juce::jmax<juce::int64> (0, startSample), std::memory_order_release);
        loopEnd.store   (juce::jmax<juce::int64> (0, endSample),   std::memory_order_release);
    }
    juce::int64 getLoopStart() const noexcept { return loopStart.load (std::memory_order_acquire); }
    juce::int64 getLoopEnd() const noexcept   { return loopEnd.load (std::memory_order_acquire); }
    bool hasValidLoop() const noexcept        { return isLoopEnabled() && getLoopEnd() > getLoopStart(); }

    // ---- Timing context ----
    double getSampleRate() const noexcept   { return sampleRate.load (std::memory_order_acquire); }
    void setSampleRate (double sr) noexcept { if (sr > 0.0) sampleRate.store (sr, std::memory_order_release); }

    double getBpm() const noexcept          { return bpm.load (std::memory_order_acquire); }
    void setBpm (double newBpm) noexcept    { if (newBpm > 0.0) bpm.store (newBpm, std::memory_order_release); }

    int getBeatsPerBar() const noexcept     { return beatsPerBar.load (std::memory_order_acquire); }
    void setBeatsPerBar (int n) noexcept    { if (n > 0) beatsPerBar.store (n, std::memory_order_release); }

    // ---- Conversions (pure functions of the current tempo map) ----
    double secondsToBeats (double seconds) const noexcept { return seconds * getBpm() / 60.0; }
    double beatsToSeconds (double beats) const noexcept   { return beats * 60.0 / getBpm(); }

    BarBeat getBarBeat() const noexcept { return barBeatForSeconds (getPositionSeconds()); }

    BarBeat barBeatForSeconds (double seconds) const noexcept
    {
        const double totalBeats = secondsToBeats (seconds);
        const int bpb = getBeatsPerBar();

        BarBeat result;
        result.bar  = static_cast<int> (totalBeats / bpb) + 1;
        const double beatInBar = std::fmod (totalBeats, static_cast<double> (bpb));
        result.beat = static_cast<int> (beatInBar) + 1;
        result.tick = static_cast<int> ((beatInBar - std::floor (beatInBar)) * ticksPerBeat);
        return result;
    }

private:
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> position { 0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<juce::int64> loopStart { 0 }, loopEnd { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> bpm { 120.0 };
    std::atomic<int> beatsPerBar { 4 };
};

} // namespace beatmaker::engine
