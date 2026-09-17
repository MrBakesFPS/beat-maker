// Metronome: a click on every beat while the transport runs, accented on the
// bar's first beat. It lives in the AudioGraph and plays straight to the main
// outputs after the master, so it is heard live but never lands in a bounce
// (the Bouncer renders on a graph of its own with the metronome off). Settings
// are atomics written from the message thread; the click itself is synthesized
// (no samples to load) and allocation-free.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

namespace beatmaker::engine
{

class Metronome
{
public:
    enum class Sound { beep, click, wood };
    static constexpr int numSounds = 3;
    static const char* soundName (int s) noexcept { switch (s) { case 0: return "Beep"; case 1: return "Click"; default: return "Wood"; } }

    void setEnabled (bool on) noexcept       { enabled.store (on, std::memory_order_relaxed); }
    bool isEnabled() const noexcept          { return enabled.load (std::memory_order_relaxed); }
    void setLevel (float linear) noexcept    { level.store (juce::jlimit (0.0f, 2.0f, linear), std::memory_order_relaxed); }
    float getLevel() const noexcept          { return level.load (std::memory_order_relaxed); }
    void setAccent (bool on) noexcept        { accent.store (on, std::memory_order_relaxed); }
    void setRecordOnly (bool on) noexcept    { recordOnly.store (on, std::memory_order_relaxed); }   // silent unless recording
    void setSound (int s) noexcept           { sound.store (juce::jlimit (0, numSounds - 1, s), std::memory_order_relaxed); }

    // Audio thread. Adds the clicks whose beats fall in [pos, pos + numSamples) to the first two outputs.
    void render (float* const* outs, int numOuts, int numSamples, juce::int64 pos, double sampleRate, double bpm, int beatsPerBar,
                 bool playing, bool recording, bool force = false) noexcept   // force: a count-in clicks whatever the settings say
    {
        if (! playing || bpm <= 0.0 || sampleRate <= 0.0 || (! force && (! isEnabled() || (recordOnly.load (std::memory_order_relaxed) && ! recording))))
        {
            voice = -1;
            return;
        }
        const double samplesPerBeat = sampleRate * 60.0 / bpm;
        const juce::int64 end = pos + numSamples;

        // The beats that start inside this block (a block never holds more than a handful)
        std::array<int, 16> starts {};
        std::array<bool, 16> accents {};
        int count = 0;
        for (auto k = (juce::int64) std::ceil ((double) pos / samplesPerBeat - 1.0e-9); count < (int) starts.size(); ++k)
        {
            const auto at = (juce::int64) std::llround ((double) k * samplesPerBeat);
            if (at >= end) break;
            if (at < pos) continue;
            starts[(size_t) count] = (int) (at - pos);
            accents[(size_t) count] = accent.load (std::memory_order_relaxed) && beatsPerBar > 0 && (k % beatsPerBar) == 0;
            ++count;
        }
        if (count == 0 && voice < 0) return;

        const float lvl = level.load (std::memory_order_relaxed);
        const int s = sound.load (std::memory_order_relaxed);
        const int length = lengthSamples (s, sampleRate);
        const int channels = juce::jmin (2, numOuts);
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < count; ++c)
                if (starts[(size_t) c] == i) { voice = 0; voiceAccent = accents[(size_t) c]; }
            if (voice < 0) continue;
            const float g = lvl * sample (s, voice, voiceAccent, sampleRate);
            for (int ch = 0; ch < channels; ++ch)
                if (outs[ch] != nullptr) outs[ch][i] += g;
            if (++voice >= length) voice = -1;
        }
    }

    void reset() noexcept { voice = -1; }

private:
    static int lengthSamples (int s, double sampleRate) noexcept
    {
        const double ms = s == 1 ? 20.0 : s == 2 ? 45.0 : 70.0;
        return juce::jmax (1, (int) (ms * 0.001 * sampleRate));
    }
    // One sample of the click, `t` samples after its start
    static float sample (int s, int t, bool accented, double sampleRate) noexcept
    {
        const double time = (double) t / sampleRate;
        const float amp = accented ? 1.0f : 0.6f;
        switch (s)
        {
            case 1:   // Click: a very short bright tick
            {
                const double f = accented ? 3200.0 : 2400.0;
                return amp * (float) (std::sin (juce::MathConstants<double>::twoPi * f * time) * std::exp (-time / 0.003));
            }
            case 2:   // Wood: two inharmonic partials, dead quickly
            {
                const double f = accented ? 980.0 : 740.0;
                const double body = std::sin (juce::MathConstants<double>::twoPi * f * time) + 0.4 * std::sin (juce::MathConstants<double>::twoPi * f * 2.76 * time);
                return amp * 0.7f * (float) (body * std::exp (-time / 0.008));
            }
            default:  // Beep: a clean tone with a short tail
            {
                const double f = accented ? 1760.0 : 1320.0;
                return amp * 0.8f * (float) (std::sin (juce::MathConstants<double>::twoPi * f * time) * std::exp (-time / 0.014));
            }
        }
    }

    std::atomic<bool> enabled { false }, accent { true }, recordOnly { false };
    std::atomic<float> level { 0.5f };
    std::atomic<int> sound { 0 };
    int voice = -1;            // samples into the current click, -1 = silent
    bool voiceAccent = false;
};

} // namespace beatmaker::engine
