// RenderSnapshot: an immutable, audio-thread-safe description of what to
// play. The message thread builds a new snapshot whenever the session
// changes and hands it to the AudioGraph, which swaps it in between blocks.
// Audio data is shared by pointer and never copied or freed on the audio
// thread.
#pragma once

#include "../dsp/DrumKit.h"
#include "../dsp/Fades.h"
#include "../dsp/SynthParams.h"
#include "../sequencer/MidiSequence.h"
#include "../sequencer/StepPattern.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <memory>
#include <vector>

namespace beatmaker::engine
{

// Pan law: -3 dB centre-compensated (centre = unity, hard side = +3 dB), the
// Pro Tools default. pan is -1 (left) .. +1 (right). Channels beyond the
// first stereo pair are unaffected.
inline float panGainForChannel (float pan, int channel) noexcept
{
    if (channel > 1 || std::abs (pan) < 1.0e-6f) return 1.0f;   // centre is exactly unity
    const float theta = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float g = channel == 0 ? std::cos (theta) : std::sin (theta);
    return g * juce::MathConstants<float>::sqrt2;
}

struct RenderClip
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;  // already at engine sample rate
    juce::int64 timelineStart = 0;   // in engine samples
    juce::int64 sourceOffset  = 0;   // first sample of `audio` to play
    juce::int64 length        = 0;   // samples to play
    float gain                = 1.0f; // clip gain * track gain, linear
    float pan                 = 0.0f; // -1..1
    juce::int64 fadeIn        = 0;   // samples, from the clip start
    juce::int64 fadeOut       = 0;   // samples, to the clip end
    FadeShape fadeInShape     = FadeShape::linear;
    FadeShape fadeOutShape    = FadeShape::linear;
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
    float pan                 = 0.0f;
    juce::int64 loopOffset    = 0;    // pattern position at timelineStart
};

// A synth instrument: the graph keeps a voice pool per instrumentId.
struct RenderSynth
{
    int instrumentId = 0;
    std::shared_ptr<const SynthParams> params;
    float pan = 0.0f;
};

// A MIDI clip driving a synth: the sequence loops for `length` samples.
struct RenderMidiClip
{
    std::shared_ptr<const MidiSequence> sequence;
    int instrumentId = 0;
    juce::int64 timelineStart = 0;
    juce::int64 length        = 0;
    float gain                = 1.0f;
    juce::int64 loopOffset    = 0;    // sequence position at timelineStart
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
    std::vector<RenderSynth> synths;
    std::vector<RenderMidiClip> midiClips;
    std::vector<MonitorInput> monitors;

    // Library audition: played from its start whenever the pointer changes,
    // independent of the transport, looping while present.
    std::shared_ptr<const juce::AudioBuffer<float>> preview;
    float previewGain = 0.8f;
    float masterGain = 1.0f;
};

} // namespace beatmaker::engine
