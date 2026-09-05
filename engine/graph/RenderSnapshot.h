// RenderSnapshot: an immutable, audio-thread-safe description of what to
// play. The message thread builds a new snapshot whenever the session
// changes and hands it to the AudioGraph, which swaps it in between blocks.
// Audio data is shared by pointer and never copied or freed on the audio
// thread.
#pragma once

#include "../automation/Automation.h"
#include "../dsp/DrumKit.h"
#include "../dsp/Effects.h"
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
    float gain                = 1.0f; // clip gain, linear (the track fader lives on the strip)
    int strip                 = 0;   // channel strip that receives this clip
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
    int strip                 = 0;
    juce::int64 loopOffset    = 0;    // pattern position at timelineStart
};

// A synth instrument: the graph keeps a voice pool per instrumentId.
struct RenderSynth
{
    int instrumentId = 0;
    std::shared_ptr<const SynthParams> params;
    int strip = 0;
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
    int strip      = 0;
};

//==============================================================================
// Mixer

struct RenderInsert
{
    std::shared_ptr<Effect> fx;                    // stateful instance owned by the model
    std::shared_ptr<const InsertParams> params;
    bool bypass = false;
    int slot = 0;                                  // insert slot, for automation lookup
};

struct RenderSend
{
    int bus = -1;          // 0..numBuses-1
    float gain = 1.0f;     // linear
    bool preFader = false;
    int slot = 0;          // send slot, for automation lookup
};

struct RenderAutomation
{
    std::shared_ptr<const AutomationLane> lane;
    bool bypass = false;   // being written right now: follow the live value instead
};

// One channel strip: sources -> inserts -> (pre sends) -> fader/pan -> (post sends) -> output.
struct RenderStrip
{
    int trackId = 0;
    bool isAux = false;    // takes its input from `inputBus` instead of sources
    int inputBus = -1;
    int outputBus = -1;    // -1 = main mix
    float gain = 1.0f;
    float pan = 0.0f;
    bool muted = false;    // after solo logic
    std::vector<RenderInsert> inserts;
    std::vector<RenderSend> sends;
    std::vector<RenderAutomation> automation;   // read when automationRead is true
    bool automationRead = false;

    int delaySamples = 0;      // delay compensation + user offset, applied after the inserts
    int outputChannel = -1;    // >= 0: direct to this device channel pair, bypassing the master
};

struct RenderMaster
{
    float gain = 1.0f;
    std::vector<RenderInsert> inserts;
};

struct RenderSnapshot
{
    std::vector<RenderClip> clips;
    std::vector<RenderPattern> patterns;
    std::vector<RenderSynth> synths;
    std::vector<RenderMidiClip> midiClips;
    std::vector<MonitorInput> monitors;

    // Channel strips in track order; empty = a single pass-through strip.
    std::vector<RenderStrip> strips;
    RenderMaster master;
    int mainOutputChannel = 0;   // first device channel of the Main output path

    // Library audition: played from its start whenever the pointer changes,
    // independent of the transport, looping while present.
    std::shared_ptr<const juce::AudioBuffer<float>> preview;
    float previewGain = 0.8f;
    float masterGain = 1.0f;
};

} // namespace beatmaker::engine
