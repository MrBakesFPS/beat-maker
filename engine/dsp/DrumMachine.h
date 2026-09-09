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
    // gateSamples > 0 fades the voice out after that many played samples (a held step); 0 lets it ring out.
    void trigger (const DrumKit* kit, int pad, float velocity, float gain, int delaySamples, int strip = 0, int gateSamples = 0) noexcept;

    // Mix active voices into the outputs (additive). strip == -1 renders every
    // voice; otherwise only voices belonging to that channel strip (a negative
    // strip other than -1 is a private one, such as the audition kit's).
    void render (float* const* outputs, int numOutputs, int numSamples, int strip = -1) noexcept;

    // Fades out every voice on a strip (the way a re-trigger chokes a pad).
    void chokeStrip (int strip) noexcept;

    bool hasVoicesForStrip (int strip) const noexcept;

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
        int strip = 0;
        bool active = false;
    };

    std::array<Voice, maxVoices> voices;
};

} // namespace beatmaker::engine
