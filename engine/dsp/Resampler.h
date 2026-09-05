// Offline resampling helpers (message/background thread only).
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace beatmaker::engine
{

class Resampler
{
public:
    // Returns a new buffer whose length is source.getNumSamples() / ratio,
    // where ratio = input samples consumed per output sample. ratio 2.0 halves
    // the length (plays twice as fast, an octave up): classic varispeed.
    static juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, double ratio);

    // Ratio that plays material recorded at `sourceBpm` back at `targetBpm`.
    static double ratioForTempo (double sourceBpm, double targetBpm) noexcept
    {
        return (sourceBpm > 0.0 && targetBpm > 0.0) ? targetBpm / sourceBpm : 1.0;
    }
};

} // namespace beatmaker::engine
