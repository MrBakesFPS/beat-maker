// StepPattern: a grid of velocities, pads x steps. Immutable once shared
// with the engine (copy-on-write from the model).
#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>

namespace beatmaker::engine
{

struct StepPattern
{
    static constexpr int maxPads  = 16;
    static constexpr int maxSteps = 256;   // 16 bars of sixteenths: room to unroll a looping clip

    int numSteps     = 16;   // 16 sixteenth notes = one 4/4 bar
    int stepsPerBeat = 4;

    // 0 = off, 1..127 = velocity
    std::array<std::array<std::uint8_t, maxSteps>, maxPads> velocity {};

    std::uint8_t get (int pad, int step) const noexcept
    {
        return juce::isPositiveAndBelow (pad, maxPads) && juce::isPositiveAndBelow (step, maxSteps)
                 ? velocity[(size_t) pad][(size_t) step] : (std::uint8_t) 0;
    }

    void set (int pad, int step, std::uint8_t v) noexcept
    {
        if (juce::isPositiveAndBelow (pad, maxPads) && juce::isPositiveAndBelow (step, maxSteps))
            velocity[(size_t) pad][(size_t) step] = v;
    }

    double getLengthBeats() const noexcept { return (double) numSteps / stepsPerBeat; }

    // Samples per step at the given tempo.
    double getStepDurationSamples (double sampleRate, double bpm) const noexcept
    {
        return sampleRate * 60.0 / (bpm * stepsPerBeat);
    }

    // A simple four-on-the-floor starting point so a new drum track makes
    // sound immediately. Pad indices follow DrumKitFactory's default layout.
    static StepPattern createDefaultBeat()
    {
        StepPattern p;
        for (int s : { 0, 8 })       p.set (0, s, 110);   // kick
        p.set (0, 10, 80);
        for (int s : { 4, 12 })      p.set (1, s, 100);   // snare
        for (int s = 0; s < 16; s += 2) p.set (4, s, s % 4 == 0 ? 90 : 60); // closed hat
        p.set (5, 14, 70);                                // open hat
        return p;
    }
};

} // namespace beatmaker::engine
