// RenderSnapshot: an immutable, audio-thread-safe description of what to
// play. The message thread builds a new snapshot whenever the session
// changes and hands it to the AudioGraph, which swaps it in between blocks.
// Audio data is shared by pointer and never copied or freed on the audio
// thread.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

namespace beatmaker::engine
{

struct RenderClip
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;  // already at engine sample rate
    juce::int64 timelineStart = 0;   // in engine samples
    juce::int64 sourceOffset  = 0;   // first sample of `audio` to play
    juce::int64 length        = 0;   // samples to play
    float gain                = 1.0f; // clip gain * track gain, linear
};

struct RenderSnapshot
{
    std::vector<RenderClip> clips;
    float masterGain = 1.0f;
};

} // namespace beatmaker::engine
