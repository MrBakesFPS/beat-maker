// Instruments: stateful voice pools owned by the model and driven by the
// AudioGraph, with immutable parameter sets (copy-on-write) passed in per
// call, exactly like Effects. Every instrument describes its parameters so
// the UI can build knobs and preset menus generically.
#pragma once

#include "Effects.h"   // ParamInfo

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

namespace beatmaker::engine
{

enum class InstrumentType { none, subtractive, fm, wavetable, sampler, electricPiano, bass };

struct InstrumentParams
{
    InstrumentType type = InstrumentType::none;
    juce::String presetName;
    std::array<float, 24> values {};

    // Sampler: the sound it plays (shared, immutable)
    std::shared_ptr<const juce::AudioBuffer<float>> sample;
    double sampleRate = 44100.0;   // of `sample`
    int rootNote = 60;
    juce::String sampleName;
};

class Instrument
{
public:
    explicit Instrument (InstrumentType t) : type (t) {}
    virtual ~Instrument() = default;

    InstrumentType getType() const noexcept { return type; }
    double getSampleRate() const noexcept { return sampleRate; }

    void prepare (double sr) { sampleRate = sr > 0.0 ? sr : sampleRate; prepareImpl(); reset(); }
    virtual void reset() = 0;

    // gateSamples < 0 holds until noteOff. Gain multiplies the whole voice.
    virtual void noteOn (int pitch, float velocity, float gain, int delaySamples, int gateSamples, const InstrumentParams&) noexcept = 0;
    virtual void noteOff (int pitch) noexcept = 0;
    virtual void allNotesOff (bool immediate) noexcept = 0;
    virtual void render (float* const* outputs, int numOutputs, int numSamples, const InstrumentParams&) noexcept = 0;
    virtual int getNumActiveVoices() const noexcept = 0;

    static std::unique_ptr<Instrument> create (InstrumentType, double sampleRate);
    static const std::vector<ParamInfo>& paramInfo (InstrumentType);
    static std::vector<InstrumentParams> presets (InstrumentType);
    static InstrumentParams defaultParams (InstrumentType);
    static const char* typeName (InstrumentType);
    static const std::vector<InstrumentType>& availableTypes();

protected:
    virtual void prepareImpl() {}
    double sampleRate = 44100.0;

private:
    InstrumentType type;
};

//==============================================================================
// Parameter indices per instrument (values[] layout)

struct SubtractiveParams   { enum { wave, osc2, detune, cutoff, resonance, filterEnv, attack, decay, sustain, release, level }; };
struct FmParams            { enum { ratio, index, indexDecay, feedback, attack, decay, sustain, release, cutoff, level }; };
struct WavetableParams     { enum { position, detune, cutoff, resonance, filterEnv, attack, decay, sustain, release, level }; };
struct SamplerParams       { enum { tune, loop, attack, decay, sustain, release, cutoff, level }; };
struct ElectricPianoParams { enum { tone, decay, release, tremoloRate, tremoloDepth, bell, level }; };
struct BassParams          { enum { wave, sub, glide, cutoff, resonance, filterEnv, decay, drive, level }; };

} // namespace beatmaker::engine
