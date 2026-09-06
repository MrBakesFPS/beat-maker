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
#include "../dsp/Instrument.h"
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
// Centre-compensated pan law. `depthDb` is the Pro Tools "pan depth": how
// far the centre sits below a hard pan (2.5, 3, 4.5 or 6 dB). The centre is
// always exactly unity; hard left/right gain +depth dB on their channel.
inline float panGainForChannel (float pan, int channel, float depthDb = 3.0f) noexcept
{
    if (channel > 1 || std::abs (pan) < 1.0e-6f) return 1.0f;   // centre is exactly unity
    const float theta = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float g = channel == 0 ? std::cos (theta) : std::sin (theta);
    if (std::abs (depthDb - 3.0f) < 0.05f) return g * juce::MathConstants<float>::sqrt2;
    const float exponent = juce::jlimit (0.1f, 12.0f, depthDb) / 3.0103f;   // cos^p: centre lands depth dB down
    return std::pow (juce::jmax (0.0f, g), exponent) * juce::Decibels::decibelsToGain (depthDb);   // cos(pi/2) is -4e-8 in float: keep pow real
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
    std::shared_ptr<const AutomationLane> gainLane;   // clip gain breakpoints, times in source samples
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

// An instrument track: the stateful voice pool lives in the model (like an
// Effect) and the snapshot keeps it alive for the audio thread; params are
// immutable and swapped copy-on-write.
struct RenderInstrument
{
    int instrumentId = 0;
    std::shared_ptr<Instrument> instance;
    std::shared_ptr<const InstrumentParams> params;
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
    MidiRealtimeProps props;          // track real-time properties
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
    int keyBus = -1;                               // sidechain key input: bus index, -1 = internal
    bool keyListen = false;                        // audition the key signal instead of the effect output
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
    float trimGain = 1.0f;                       // live Trim-mode offset on top of the volume lane

    int delaySamples = 0;      // delay compensation + user offset, applied after the inserts
    int outputChannel = -1;    // >= 0: direct to this device channel pair, bypassing the master
    int vcaStrip = -1;         // strip index of the VCA master scaling this strip's fader (gain + volume automation)
    bool isVca = false;        // VCA masters carry no audio
};

struct RenderMaster
{
    float gain = 1.0f;
    std::vector<RenderInsert> inserts;
    std::vector<RenderAutomation> automation;
    bool automationRead = false;
    float trimGain = 1.0f;
};

struct RenderSnapshot
{
    std::vector<RenderClip> clips;
    std::vector<RenderPattern> patterns;
    std::vector<RenderInstrument> instruments;
    std::vector<RenderMidiClip> midiClips;
    std::vector<MonitorInput> monitors;

    // Channel strips in track order; empty = a single pass-through strip.
    std::vector<RenderStrip> strips;
    // Processing order of the strips (indices into `strips`); empty = natural
    // order. Built so a strip whose insert is keyed from a bus comes after
    // the strips that send to that bus, giving zero-latency sidechains.
    std::vector<int> stripOrder;
    RenderMaster master;
    int mainOutputChannel = 0;   // first device channel of the Main output path

    // Library audition: played from its start whenever the pointer changes,
    // independent of the transport, looping while present.
    std::shared_ptr<const juce::AudioBuffer<float>> preview;
    float previewGain = 0.8f;
    float masterGain = 1.0f;
    float panDepthDb = 3.0f;   // pan law depth (preference)
};

} // namespace beatmaker::engine
