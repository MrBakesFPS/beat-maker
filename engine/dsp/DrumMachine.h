// DrumMachine: a real-time sample voice pool. Triggers come from the step
// sequencer and from pad previews; voices reference kit samples by raw
// pointer, which is safe because the AudioGraph kills every voice whose kit
// leaves the active snapshot before that snapshot is retired.
#pragma once

#include "DrumKit.h"
#include <array>

namespace beatmaker::engine
{

class DrumMachine
{
public:
    static constexpr int maxVoices = 64;
    static constexpr int retriggerFadeSamples = 64;

    // Start `pad` of `kit` after `delaySamples` within the next render call.
    // Re-triggering a pad that is still sounding fades the old voice out.
    void trigger (const DrumKit* kit, int pad, float velocity, float gain, int delaySamples) noexcept;

    // Mix all active voices into the outputs (additive).
    void render (float* const* outputs, int numOutputs, int numSamples) noexcept;

    // Kill every voice whose kit is not in `kits`.
    void killVoicesNotUsing (const DrumKit* const* kits, int numKits) noexcept;

    void reset() noexcept;
    int getNumActiveVoices() const noexcept;

private:
    struct Voice
    {
        const DrumKit* kit = nullptr;
        const juce::AudioBuffer<float>* audio = nullptr;
        int pad = -1;
        int position = 0;
        int delay = 0;
        int fadePending = -1;     // >= 0: samples until a choke fade begins
        int fadeRemaining = -1;   // >= 0 while fading out
        float gain = 0.0f;
        bool active = false;
    };

    std::array<Voice, maxVoices> voices;
};

} // namespace beatmaker::engine
