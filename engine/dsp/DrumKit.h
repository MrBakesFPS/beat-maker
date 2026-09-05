// DrumKit: 16 pads, each holding an immutable sample. Shared by pointer with
// the audio thread, so a kit is never modified after it is handed over.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>

namespace beatmaker::engine
{

struct DrumSample
{
    juce::String name;
    std::shared_ptr<const juce::AudioBuffer<float>> audio; // at engine sample rate
    float gain = 1.0f;
    juce::String sourcePath;   // file the sample came from (empty for synthesised pads)
};

struct DrumKit
{
    static constexpr int numPads = 16;

    juce::String name;
    std::array<DrumSample, numPads> pads;
};

} // namespace beatmaker::engine
