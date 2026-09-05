// Onset (transient) detection for Beat Detective-style tools, audio quantize
// and tab-to-transient. Offline; message or background thread only.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace beatmaker::engine
{

class Transients
{
public:
    // Returns onset positions (samples) in ascending order. `sensitivity`
    // 0..1: higher finds softer hits. Onsets closer than `minSpacingSeconds`
    // to the previous one are dropped.
    static std::vector<juce::int64> detect (const juce::AudioBuffer<float>& audio, double sampleRate,
                                            float sensitivity = 0.5f, double minSpacingSeconds = 0.05);

    static constexpr int hopSize = 128;
};

} // namespace beatmaker::engine
