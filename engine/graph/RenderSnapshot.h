// RenderSnapshot: an immutable, audio-thread-safe description of what to
// play. The message thread builds a new snapshot whenever the session
// changes and hands it to the AudioGraph, which swaps it in between blocks.
// Audio data is shared by pointer and never copied or freed on the audio
// thread.
#pragma once

#include "../dsp/DrumKit.h"
#include "../sequencer/StepPattern.h"

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

// A pattern clip: the step pattern loops for `length` samples starting at
// `timelineStart`, triggering pads of `kit`.
struct RenderPattern
{
    std::shared_ptr<const StepPattern> pattern;
    std::shared_ptr<const DrumKit> kit;
    juce::int64 timelineStart = 0;
    juce::int64 length        = 0;
    float gain                = 1.0f;
};

// Pass a device input straight to the outputs (input monitoring).
struct MonitorInput
{
    int firstInput = 0;
    int numInputs  = 1;   // 1 = mono to all outputs, 2 = stereo pair
    float gain     = 1.0f;
};

struct RenderSnapshot
{
    std::vector<RenderClip> clips;
    std::vector<RenderPattern> patterns;
    std::vector<MonitorInput> monitors;

    // Library audition: played from its start whenever the pointer changes,
    // independent of the transport, looping while present.
    std::shared_ptr<const juce::AudioBuffer<float>> preview;
    float previewGain = 0.8f;
    float masterGain = 1.0f;
};

} // namespace beatmaker::engine
