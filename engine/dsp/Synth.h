// Synth: a polyphonic subtractive synthesizer voice pool. One instance per
// instrument track lives inside the AudioGraph; parameters are read from an
// immutable SynthParams kept alive by the active snapshot.
//
// Notes carry their own gate length so the sequencer never has to schedule a
// matching note-off across blocks; pass gateSamples < 0 to hold until
// noteOff().
#pragma once

#include "SynthParams.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace beatmaker::engine
{

class Synth
{
public:
    static constexpr int maxVoices = 16;

    void prepare (double sampleRate) noexcept;
    void setParams (const SynthParams* p) noexcept { params = p; }
    const SynthParams* getParams() const noexcept { return params; }
    void setPan (float newPan) noexcept { pan = juce::jlimit (-1.0f, 1.0f, newPan); }

    void noteOn (int pitch, float velocity, int delaySamples, int gateSamples) noexcept;
    void noteOff (int pitch) noexcept;
    void allNotesOff (bool immediate = false) noexcept;

    void render (float* const* outputs, int numOutputs, int numSamples) noexcept;

    int getNumActiveVoices() const noexcept;
    void reset() noexcept;

private:
    enum class Stage { attack, decay, sustain, release, off };

    struct Voice
    {
        bool active = false;
        int pitch = 0;
        float velocity = 0.0f;
        int delay = 0;
        int gate = -1;               // samples until auto-release, -1 = held
        Stage stage = Stage::off;
        float env = 0.0f;
        float releaseStep = 0.0f;
        double phase1 = 0.0, phase2 = 0.0, inc1 = 0.0, inc2 = 0.0;
        float ic1eq = 0.0f, ic2eq = 0.0f;   // SVF state
        std::uint32_t order = 0;
    };

    static float polyBlep (double t, double dt) noexcept;
    static float oscillator (SynthParams::Wave, double phase, double inc) noexcept;
    void startVoice (Voice&, int pitch, float velocity, int delay, int gate) noexcept;
    void advanceEnvelope (Voice&) noexcept;

    std::array<Voice, maxVoices> voices;
    const SynthParams* params = nullptr;
    float pan = 0.0f;
    double sampleRate = 44100.0;
    std::uint32_t orderCounter = 0;
    static constexpr int coefficientInterval = 16;
};

} // namespace beatmaker::engine
