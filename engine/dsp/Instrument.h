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

// Session files store the type as its number: new instruments go at the end.
enum class InstrumentType { none, subtractive, fm, wavetable, sampler, electricPiano, bass, pluck, organ, stack, chip, vox,
                            piano, strings, mallets, brass, flute };

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
    juce::String samplePath;   // file the sample came from (for session files)
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
    // Where an instrument sits in the chooser (Synths, Keys, Bass, Samplers, ...) and a line about what it is for
    static const char* typeCategory (InstrumentType);
    static const char* typeDescription (InstrumentType);
    static const std::vector<InstrumentType>& availableTypes();
    static std::vector<juce::String> categories();   // in display order, each with at least one instrument

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
struct PluckParams         { enum { brightness, damping, decay, position, body, release, level }; };
struct OrganParams         { enum { bar16, bar5, bar8, bar4, bar2b3, bar2, bar1b3b5, bar1b1b3, bar1, percussion, percDecay, click, vibrato, vibratoRate, level }; };
struct StackParams         { enum { voices, detune, width, mix, cutoff, resonance, filterEnv, attack, decay, sustain, release, level }; };
struct ChipParams          { enum { wave, bits, arp, arpRate, vibratoRate, vibratoDepth, attack, decay, sustain, release, level }; };
struct VoxParams           { enum { vowel, drift, driftRate, breath, width, tone, attack, decay, sustain, release, level }; };
struct PianoParams         { enum { hardness, stiffness, decay, detune, thump, tone, release, level }; };
struct StringsParams       { enum { ensemble, movement, vibrato, vibratoRate, bow, attack, decay, sustain, release, level }; };
struct MalletParams        { enum { instrument, hardness, decay, tremoloRate, tremoloDepth, strike, release, level }; };
struct BrassParams         { enum { detune, blat, blatTime, cutoff, resonance, dip, vibrato, vibratoRate, attack, release, level }; };
struct FluteParams         { enum { breath, air, chiff, overblow, vibrato, vibratoRate, vibratoDelay, attack, release, level }; };

} // namespace beatmaker::engine
