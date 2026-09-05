#include "Instrument.h"
#include "VoiceHelpers.h"
#include <array>
#include <cmath>

namespace beatmaker::engine
{

using namespace dsp;

//==============================================================================
// Metadata

const std::vector<InstrumentType>& Instrument::availableTypes()
{
    static const std::vector<InstrumentType> types { InstrumentType::subtractive, InstrumentType::fm, InstrumentType::wavetable,
                                                     InstrumentType::sampler, InstrumentType::electricPiano, InstrumentType::bass };
    return types;
}

const char* Instrument::typeName (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::none:          return "None";
        case InstrumentType::subtractive:   return "Synth";
        case InstrumentType::fm:            return "FM Synth";
        case InstrumentType::wavetable:     return "Wavetable";
        case InstrumentType::sampler:       return "Sampler";
        case InstrumentType::electricPiano: return "Electric Piano";
        case InstrumentType::bass:          return "Bass";
    }
    return "";
}

const std::vector<ParamInfo>& Instrument::paramInfo (InstrumentType t)
{
    static const std::vector<ParamInfo> none;
    static const std::vector<ParamInfo> sub {
        { "Wave",      0.0f, 3.0f,      0.0f,   0.0f,   "" },        // saw, square, triangle, sine
        { "Osc 2",     0.0f, 1.0f,      1.0f,   0.0f,   "" },
        { "Detune",    0.0f, 50.0f,     7.0f,  10.0f,   " ct" },
        { "Cutoff",   20.0f, 20000.0f, 6000.0f, 1000.0f, " Hz" },
        { "Reso",      0.0f, 1.0f,      0.1f,   0.5f,   "" },
        { "Filt Env",  0.0f, 6.0f,      0.0f,   3.0f,   " oct" },
        { "Attack",  0.001f, 4.0f,     0.005f,  0.1f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.2f,   0.3f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.7f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.3f,   0.5f,   " s" },
        { "Level",     0.0f, 1.0f,      0.4f,   0.4f,   "" } };
    static const std::vector<ParamInfo> fm {
        { "Ratio",     0.5f, 12.0f,     2.0f,   3.0f,   "" },
        { "Index",     0.0f, 12.0f,     3.0f,   3.0f,   "" },
        { "Idx Decay", 0.01f, 4.0f,     0.4f,   0.4f,   " s" },
        { "Feedback",  0.0f, 1.0f,      0.0f,   0.0f,   "" },
        { "Attack",  0.001f, 4.0f,     0.002f,  0.1f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.6f,   0.4f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.3f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.5f,   0.5f,   " s" },
        { "Cutoff",   20.0f, 20000.0f, 12000.0f, 2000.0f, " Hz" },
        { "Level",     0.0f, 1.0f,      0.3f,   0.4f,   "" } };
    static const std::vector<ParamInfo> wt {
        { "Position",  0.0f, 1.0f,      0.3f,   0.5f,   "" },
        { "Detune",    0.0f, 50.0f,     9.0f,  10.0f,   " ct" },
        { "Cutoff",   20.0f, 20000.0f, 5000.0f, 1000.0f, " Hz" },
        { "Reso",      0.0f, 1.0f,      0.15f,  0.5f,   "" },
        { "Filt Env",  0.0f, 6.0f,      1.0f,   3.0f,   " oct" },
        { "Attack",  0.001f, 4.0f,      0.01f,  0.1f,   " s" },
        { "Decay",    0.01f, 4.0f,      0.5f,   0.3f,   " s" },
        { "Sustain",   0.0f, 1.0f,      0.6f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.4f,   0.5f,   " s" },
        { "Level",     0.0f, 1.0f,      0.35f,  0.4f,   "" } };
    static const std::vector<ParamInfo> smp {
        { "Tune",    -24.0f, 24.0f,     0.0f,   0.0f,   " st" },
        { "Loop",      0.0f, 1.0f,      0.0f,   0.0f,   "" },
        { "Attack",  0.001f, 4.0f,     0.002f,  0.1f,   " s" },
        { "Decay",    0.01f, 8.0f,      8.0f,   1.0f,   " s" },
        { "Sustain",   0.0f, 1.0f,      1.0f,   0.5f,   "" },
        { "Release",  0.01f, 6.0f,      0.2f,   0.5f,   " s" },
        { "Cutoff",   20.0f, 20000.0f, 20000.0f, 2000.0f, " Hz" },
        { "Level",     0.0f, 1.0f,      0.8f,   0.5f,   "" } };
    static const std::vector<ParamInfo> ep {
        { "Tone",      0.0f, 1.0f,      0.5f,   0.5f,   "" },
        { "Decay",     0.5f, 12.0f,     4.0f,   3.0f,   " s" },
        { "Release",  0.01f, 3.0f,      0.15f,  0.3f,   " s" },
        { "Trem Rate", 0.0f, 12.0f,     5.5f,   4.0f,   " Hz" },
        { "Trem Depth", 0.0f, 1.0f,     0.3f,   0.5f,   "" },
        { "Bell",      0.0f, 1.0f,      0.3f,   0.5f,   "" },
        { "Level",     0.0f, 1.0f,      0.6f,   0.5f,   "" } };
    static const std::vector<ParamInfo> bass {
        { "Wave",      0.0f, 1.0f,      0.0f,   0.0f,   "" },        // saw, square
        { "Sub",       0.0f, 1.0f,      0.5f,   0.5f,   "" },
        { "Glide",     0.0f, 0.5f,      0.05f,  0.1f,   " s" },
        { "Cutoff",   20.0f, 20000.0f, 800.0f, 800.0f,  " Hz" },
        { "Reso",      0.0f, 1.0f,      0.3f,   0.5f,   "" },
        { "Filt Env",  0.0f, 6.0f,      2.5f,   3.0f,   " oct" },
        { "Decay",    0.01f, 4.0f,      0.35f,  0.3f,   " s" },
        { "Drive",     0.0f, 24.0f,     6.0f,   6.0f,   " dB" },
        { "Level",     0.0f, 1.0f,      0.6f,   0.5f,   "" } };

    switch (t)
    {
        case InstrumentType::subtractive:   return sub;
        case InstrumentType::fm:            return fm;
        case InstrumentType::wavetable:     return wt;
        case InstrumentType::sampler:       return smp;
        case InstrumentType::electricPiano: return ep;
        case InstrumentType::bass:          return bass;
        case InstrumentType::none:          break;
    }
    return none;
}

InstrumentParams Instrument::defaultParams (InstrumentType t)
{
    InstrumentParams p;
    p.type = t;
    p.presetName = "Init";
    const auto& info = paramInfo (t);
    for (size_t i = 0; i < info.size() && i < p.values.size(); ++i) p.values[i] = info[i].def;
    return p;
}

std::vector<InstrumentParams> Instrument::presets (InstrumentType t)
{
    std::vector<InstrumentParams> out;
    auto add = [&] (const char* name, std::initializer_list<std::pair<int, float>> changes)
    {
        auto p = defaultParams (t);
        p.presetName = name;
        for (const auto& c : changes) p.values[(size_t) c.first] = c.second;
        out.push_back (p);
    };

    switch (t)
    {
        case InstrumentType::subtractive:
            add ("Init Saw", {});
            add ("Pluck",       { { SubtractiveParams::cutoff, 900.0f }, { SubtractiveParams::resonance, 0.3f }, { SubtractiveParams::filterEnv, 3.5f },
                                  { SubtractiveParams::attack, 0.001f }, { SubtractiveParams::decay, 0.28f }, { SubtractiveParams::sustain, 0.0f },
                                  { SubtractiveParams::release, 0.2f }, { SubtractiveParams::level, 0.45f } });
            add ("Soft Pad",    { { SubtractiveParams::wave, 2.0f }, { SubtractiveParams::detune, 12.0f }, { SubtractiveParams::cutoff, 2500.0f },
                                  { SubtractiveParams::attack, 0.4f }, { SubtractiveParams::decay, 0.5f }, { SubtractiveParams::sustain, 0.8f },
                                  { SubtractiveParams::release, 0.9f }, { SubtractiveParams::level, 0.35f } });
            add ("Square Lead", { { SubtractiveParams::wave, 1.0f }, { SubtractiveParams::detune, 5.0f }, { SubtractiveParams::cutoff, 5000.0f },
                                  { SubtractiveParams::resonance, 0.2f }, { SubtractiveParams::decay, 0.1f }, { SubtractiveParams::sustain, 0.9f },
                                  { SubtractiveParams::release, 0.15f }, { SubtractiveParams::level, 0.3f } });
            add ("Sub Bass",    { { SubtractiveParams::wave, 3.0f }, { SubtractiveParams::osc2, 0.0f }, { SubtractiveParams::cutoff, 800.0f },
                                  { SubtractiveParams::attack, 0.003f }, { SubtractiveParams::decay, 0.3f }, { SubtractiveParams::sustain, 0.8f },
                                  { SubtractiveParams::release, 0.1f }, { SubtractiveParams::level, 0.7f } });
            break;
        case InstrumentType::fm:
            add ("FM Bell",  { { FmParams::ratio, 3.5f }, { FmParams::index, 4.0f }, { FmParams::indexDecay, 1.2f }, { FmParams::decay, 2.0f }, { FmParams::sustain, 0.0f }, { FmParams::release, 1.5f }, { FmParams::level, 0.22f } });
            add ("FM Bass",  { { FmParams::ratio, 1.0f }, { FmParams::index, 5.0f }, { FmParams::indexDecay, 0.15f }, { FmParams::decay, 0.3f }, { FmParams::sustain, 0.5f }, { FmParams::release, 0.1f }, { FmParams::level, 0.6f } });
            add ("FM Keys",  { { FmParams::ratio, 2.0f }, { FmParams::index, 2.0f }, { FmParams::indexDecay, 0.6f }, { FmParams::feedback, 0.2f }, { FmParams::decay, 1.0f }, { FmParams::sustain, 0.2f } });
            add ("FM Glass", { { FmParams::ratio, 7.0f }, { FmParams::index, 1.5f }, { FmParams::indexDecay, 2.0f }, { FmParams::attack, 0.3f }, { FmParams::sustain, 0.6f }, { FmParams::release, 2.0f } });
            break;
        case InstrumentType::wavetable:
            add ("WT Saw Sweep", {});
            add ("WT Hollow",    { { WavetableParams::position, 0.75f }, { WavetableParams::cutoff, 3000.0f }, { WavetableParams::detune, 14.0f } });
            add ("WT Pluck",     { { WavetableParams::position, 0.15f }, { WavetableParams::cutoff, 700.0f }, { WavetableParams::filterEnv, 3.0f }, { WavetableParams::decay, 0.25f }, { WavetableParams::sustain, 0.0f } });
            add ("WT Pad",       { { WavetableParams::position, 0.5f }, { WavetableParams::attack, 0.6f }, { WavetableParams::sustain, 0.9f }, { WavetableParams::release, 1.5f }, { WavetableParams::cutoff, 2500.0f } });
            break;
        case InstrumentType::sampler:
            add ("One Shot", {});
            add ("Looped",   { { SamplerParams::loop, 1.0f } });
            add ("Pad",      { { SamplerParams::loop, 1.0f }, { SamplerParams::attack, 0.3f }, { SamplerParams::release, 1.0f }, { SamplerParams::cutoff, 4000.0f } });
            break;
        case InstrumentType::electricPiano:
            add ("Tine Piano", {});
            add ("Bright EP",  { { ElectricPianoParams::tone, 0.85f }, { ElectricPianoParams::bell, 0.6f } });
            add ("Warm EP",    { { ElectricPianoParams::tone, 0.25f }, { ElectricPianoParams::tremoloDepth, 0.5f }, { ElectricPianoParams::decay, 6.0f } });
            break;
        case InstrumentType::bass:
            add ("Acid Bass",   {});
            add ("Deep Sub",    { { BassParams::wave, 0.0f }, { BassParams::sub, 1.0f }, { BassParams::cutoff, 300.0f }, { BassParams::filterEnv, 1.0f }, { BassParams::drive, 0.0f } });
            add ("Square Bass", { { BassParams::wave, 1.0f }, { BassParams::cutoff, 1200.0f }, { BassParams::resonance, 0.5f }, { BassParams::drive, 12.0f } });
            break;
        case InstrumentType::none:
            break;
    }
    if (out.empty()) out.push_back (defaultParams (t));
    return out;
}

//==============================================================================
// Common polyphonic voice pool

namespace
{
    constexpr int maxVoices = 16;

    struct VoiceBase
    {
        bool active = false;
        int pitch = 0;
        float velocity = 0.0f, gain = 1.0f;
        int delay = 0, gate = -1;
        Adsr env;
        Svf filter;
        std::uint32_t order = 0;
    };

    template <typename Voice>
    Voice* allocateVoice (std::array<Voice, maxVoices>& voices, std::uint32_t& counter)
    {
        Voice* target = nullptr;
        for (auto& v : voices) if (! v.active) { target = &v; break; }
        if (target == nullptr)
            for (auto& v : voices) if (v.env.stage == Adsr::Stage::release && (target == nullptr || v.order < target->order)) target = &v;
        if (target == nullptr) { target = &voices[0]; for (auto& v : voices) if (v.order < target->order) target = &v; }
        *target = Voice {};
        target->active = true;
        target->order = ++counter;
        return target;
    }

    // Gate countdown -> release; returns false when the voice has gone silent.
    template <typename Voice>
    bool stepGate (Voice& v, double releaseSeconds, double sr) noexcept
    {
        if (v.gate == 0 && v.env.stage != Adsr::Stage::release) { v.env.release (releaseSeconds, sr); v.gate = -1; }
        else if (v.gate > 0) --v.gate;
        return v.env.isActive();
    }

    template <typename Voice>
    void releaseAll (std::array<Voice, maxVoices>& voices, bool immediate) noexcept
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (immediate) { v.active = false; v.env.kill(); }
            else if (v.env.stage != Adsr::Stage::release) v.gate = 0;
        }
    }

    template <typename Voice>
    void noteOffAll (std::array<Voice, maxVoices>& voices, int pitch) noexcept
    {
        for (auto& v : voices) if (v.active && v.pitch == pitch && v.gate < 0 && v.env.stage != Adsr::Stage::release) v.gate = 0;
    }

    template <typename Voice>
    int countActive (const std::array<Voice, maxVoices>& voices) noexcept
    {
        int n = 0; for (const auto& v : voices) n += v.active ? 1 : 0; return n;
    }

    void addToOutputs (float* const* outputs, int numOutputs, int i, float sample) noexcept
    {
        for (int ch = 0; ch < juce::jmin (numOutputs, 32); ++ch)
            if (outputs[ch] != nullptr) outputs[ch][i] += sample;
    }
}

//==============================================================================
// Subtractive synth (two oscillators, SVF, ADSR)

class SubtractiveSynth final : public Instrument
{
    struct Voice : VoiceBase { double phase1 = 0.0, phase2 = 0.37, inc1 = 0.0, inc2 = 0.0; };
public:
    SubtractiveSynth() : Instrument (InstrumentType::subtractive) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const double f = midiToHz (pitch), det = p.values[SubtractiveParams::detune];
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc1 = f * std::pow (2.0, -det / 1200.0) / sampleRate;
        v.inc2 = f * std::pow (2.0,  det / 1200.0) / sampleRate;
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const auto wave = (Wave) juce::jlimit (0, 3, (int) std::lround (pv[SubtractiveParams::wave]));
        const bool osc2 = pv[SubtractiveParams::osc2] >= 0.5f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[SubtractiveParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[SubtractiveParams::attack], pv[SubtractiveParams::decay], pv[SubtractiveParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                if (((i - offset) & 15) == 0)
                    v.filter.setCutoff (pv[SubtractiveParams::cutoff] * std::pow (2.0, pv[SubtractiveParams::filterEnv] * env), pv[SubtractiveParams::resonance], sampleRate);
                float osc = oscillator (wave, v.phase1, v.inc1);
                v.phase1 += v.inc1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
                if (osc2) { osc = 0.5f * (osc + oscillator (wave, v.phase2, v.inc2)); v.phase2 += v.inc2; if (v.phase2 >= 1.0) v.phase2 -= 1.0; }
                addToOutputs (outputs, numOutputs, i, v.filter.process (osc) * env * v.velocity * v.gain * pv[SubtractiveParams::level]);
            }
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
};

//==============================================================================
// FM synth: one modulator into one carrier, index decays over time

class FmSynth final : public Instrument
{
    struct Voice : VoiceBase { double cPhase = 0.0, mPhase = 0.0, cInc = 0.0, mInc = 0.0; float indexEnv = 1.0f, lastMod = 0.0f; };
public:
    FmSynth() : Instrument (InstrumentType::fm) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const double f = midiToHz (pitch);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.cInc = f / sampleRate; v.mInc = f * p.values[FmParams::ratio] / sampleRate; v.indexEnv = 1.0f;
        v.filter.setCutoff (p.values[FmParams::cutoff], 0.0f, sampleRate);
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const float indexDecayCoef = (float) std::exp (-1.0 / juce::jmax (1.0, pv[FmParams::indexDecay] * sampleRate));
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[FmParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[FmParams::attack], pv[FmParams::decay], pv[FmParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                v.indexEnv *= indexDecayCoef;
                const float index = pv[FmParams::index] * (0.15f + 0.85f * v.indexEnv) * (0.5f + 0.5f * v.velocity);
                const float mod = (float) std::sin (juce::MathConstants<double>::twoPi * v.mPhase + pv[FmParams::feedback] * v.lastMod);
                v.lastMod = mod;
                const float carrier = (float) std::sin (juce::MathConstants<double>::twoPi * v.cPhase + index * mod);
                v.cPhase += v.cInc; if (v.cPhase >= 1.0) v.cPhase -= 1.0;
                v.mPhase += v.mInc; if (v.mPhase >= 1.0) v.mPhase -= 1.0;
                addToOutputs (outputs, numOutputs, i, v.filter.process (carrier) * env * v.velocity * v.gain * pv[FmParams::level]);
            }
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
};

//==============================================================================
// Wavetable synth: three band-limited tables morphed by Position

class WavetableSynth final : public Instrument
{
    static constexpr int tableSize = 2048, numTables = 3;
    struct Voice : VoiceBase { double phase1 = 0.0, phase2 = 0.5, inc1 = 0.0, inc2 = 0.0; };
public:
    WavetableSynth() : Instrument (InstrumentType::wavetable)
    {
        // Table 0: saw (32 harmonics), 1: "hollow" (odd harmonics 1/n, like a square with soft top), 2: formant-ish (harmonics 1..8 with a peak at 4)
        for (int t = 0; t < numTables; ++t)
            for (int i = 0; i < tableSize; ++i)
            {
                const double ph = (double) i / tableSize;
                double v = 0.0;
                for (int h = 1; h <= 32; ++h)
                {
                    double amp = 0.0;
                    if (t == 0)      amp = 1.0 / h;
                    else if (t == 1) amp = (h % 2 == 1) ? 1.0 / h : 0.0;
                    else if (h <= 8) amp = std::exp (-0.5 * std::pow ((h - 4.0) / 1.5, 2.0));
                    v += amp * std::sin (juce::MathConstants<double>::twoPi * h * ph);
                }
                tables[(size_t) t][(size_t) i] = (float) v;
            }
        for (auto& tbl : tables)
        {
            float peak = 0.0f; for (float x : tbl) peak = juce::jmax (peak, std::abs (x));
            if (peak > 0.0f) for (auto& x : tbl) x /= peak;
        }
    }
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        const double f = midiToHz (pitch), det = p.values[WavetableParams::detune];
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc1 = f * std::pow (2.0, -det / 1200.0) / sampleRate; v.inc2 = f * std::pow (2.0, det / 1200.0) / sampleRate;
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    float read (double phase, float position) const noexcept
    {
        const float pos = juce::jlimit (0.0f, 1.0f, position) * (numTables - 1);
        const int t0 = juce::jmin (numTables - 2, (int) pos); const float frac = pos - t0;
        const double idx = phase * tableSize;
        const int i0 = ((int) idx) % tableSize, i1 = (i0 + 1) % tableSize;
        const float f = (float) (idx - std::floor (idx));
        auto sample = [&] (int t) { return tables[(size_t) t][(size_t) i0] + (tables[(size_t) t][(size_t) i1] - tables[(size_t) t][(size_t) i0]) * f; };
        return sample (t0) * (1.0f - frac) + sample (t0 + 1) * frac;
    }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[WavetableParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[WavetableParams::attack], pv[WavetableParams::decay], pv[WavetableParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                if (((i - offset) & 15) == 0)
                    v.filter.setCutoff (pv[WavetableParams::cutoff] * std::pow (2.0, pv[WavetableParams::filterEnv] * env), pv[WavetableParams::resonance], sampleRate);
                const float osc = 0.5f * (read (v.phase1, pv[WavetableParams::position]) + read (v.phase2, pv[WavetableParams::position]));
                v.phase1 += v.inc1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
                v.phase2 += v.inc2; if (v.phase2 >= 1.0) v.phase2 -= 1.0;
                addToOutputs (outputs, numOutputs, i, v.filter.process (osc) * env * v.velocity * v.gain * pv[WavetableParams::level]);
            }
        }
    }
private:
    std::array<std::array<float, tableSize>, numTables> tables {};
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
};

//==============================================================================
// Sampler: one sample pitched around its root note, one-shot or looped

class Sampler final : public Instrument
{
    struct Voice : VoiceBase { double position = 0.0, rate = 1.0; };
public:
    Sampler() : Instrument (InstrumentType::sampler) {}
    void reset() override { for (auto& v : voices) v = {}; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (p.sample == nullptr || p.sample->getNumSamples() < 2 || ! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.rate = std::pow (2.0, (pitch - p.rootNote + p.values[SamplerParams::tune]) / 12.0) * (p.sampleRate / sampleRate);
        v.filter.setCutoff (p.values[SamplerParams::cutoff], 0.0f, sampleRate);
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        if (p.sample == nullptr) { for (auto& v : voices) v.active = false; return; }
        const auto& pv = p.values;
        const auto& s = *p.sample;
        const int length = s.getNumSamples(), channels = s.getNumChannels();
        const bool loop = pv[SamplerParams::loop] >= 0.5f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int offset = juce::jmin (v.delay, n); v.delay -= offset;
            for (int i = offset; i < n; ++i)
            {
                if (! stepGate (v, pv[SamplerParams::release], sampleRate)) { v.active = false; break; }
                const float env = v.env.next (pv[SamplerParams::attack], pv[SamplerParams::decay], pv[SamplerParams::sustain], sampleRate);
                if (! v.env.isActive()) { v.active = false; break; }
                if (v.position >= length - 1)
                {
                    if (loop) v.position = std::fmod (v.position, (double) (length - 1));
                    else { v.active = false; break; }
                }
                const int i0 = (int) v.position; const float frac = (float) (v.position - i0);
                float sum = 0.0f;
                for (int ch = 0; ch < channels; ++ch) { const float* d = s.getReadPointer (ch); sum += d[i0] + (d[i0 + 1] - d[i0]) * frac; }
                v.position += v.rate;
                addToOutputs (outputs, numOutputs, i, v.filter.process (sum / (float) channels) * env * v.velocity * v.gain * pv[SamplerParams::level]);
            }
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
};

//==============================================================================
// Electric piano: tine (fundamental + inharmonic partials with faster decay) with tremolo

class ElectricPiano final : public Instrument
{
    struct Voice : VoiceBase { double ph1 = 0.0, ph2 = 0.0, ph3 = 0.0, inc = 0.0; float amp = 0.0f, ampCoef = 1.0f, brightCoef = 1.0f, bright = 1.0f; };
public:
    ElectricPiano() : Instrument (InstrumentType::electricPiano) {}
    void reset() override { for (auto& v : voices) v = {}; tremPhase = 0.0; }

    void noteOn (int pitch, float velocity, float gain, int delay, int gate, const InstrumentParams& p) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        auto& v = *allocateVoice (voices, counter);
        v.pitch = pitch; v.velocity = juce::jlimit (0.0f, 1.0f, velocity); v.gain = gain; v.delay = juce::jmax (0, delay); v.gate = gate;
        v.inc = midiToHz (pitch) / sampleRate;
        // Higher notes decay faster, like a real tine
        const double decaySeconds = p.values[ElectricPianoParams::decay] * std::pow (0.5, (pitch - 60) / 24.0);
        v.amp = 1.0f;
        v.ampCoef = (float) std::exp (-1.0 / juce::jmax (0.01, decaySeconds * sampleRate));
        v.bright = 0.3f + 0.7f * v.velocity;                                 // velocity -> attack brightness
        v.brightCoef = (float) std::exp (-1.0 / (0.25 * sampleRate));        // partials fade in ~0.25 s
        v.env.start();
    }
    void noteOff (int pitch) noexcept override { noteOffAll (voices, pitch); }
    void allNotesOff (bool immediate) noexcept override { releaseAll (voices, immediate); }
    int getNumActiveVoices() const noexcept override { return countActive (voices); }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        const auto& pv = p.values;
        const double tremInc = pv[ElectricPianoParams::tremoloRate] / sampleRate;
        const float tremDepth = pv[ElectricPianoParams::tremoloDepth], tone = pv[ElectricPianoParams::tone], bell = pv[ElectricPianoParams::bell];
        for (int i = 0; i < n; ++i)
        {
            tremPhase += tremInc; if (tremPhase >= 1.0) tremPhase -= 1.0;
            const float trem = 1.0f - tremDepth * 0.5f * (1.0f + (float) std::sin (juce::MathConstants<double>::twoPi * tremPhase));
            float mix = 0.0f;
            bool any = false;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.delay > 0) { --v.delay; continue; }
                if (! stepGate (v, pv[ElectricPianoParams::release], sampleRate)) { v.active = false; continue; }
                const float env = v.env.next (0.001, 100.0, 1.0f, sampleRate);   // held; the tine decay is v.amp
                if (! v.env.isActive()) { v.active = false; continue; }
                v.amp *= v.ampCoef; v.bright *= v.brightCoef;
                if (v.amp < 1.0e-4f) { v.active = false; continue; }
                const float fundamental = (float) std::sin (juce::MathConstants<double>::twoPi * v.ph1);
                const float second = (float) std::sin (juce::MathConstants<double>::twoPi * v.ph2) * (0.2f + 0.5f * tone) * v.bright;
                const float tineBell = (float) std::sin (juce::MathConstants<double>::twoPi * v.ph3) * bell * v.bright * 0.5f;
                v.ph1 += v.inc; if (v.ph1 >= 1.0) v.ph1 -= 1.0;
                v.ph2 += v.inc * 2.0; if (v.ph2 >= 1.0) v.ph2 -= 1.0;
                v.ph3 += v.inc * 5.9; if (v.ph3 >= 1.0) v.ph3 -= 1.0;   // inharmonic bell partial
                mix += (fundamental + second + tineBell) * v.amp * env * v.velocity * v.gain;
                any = true;
            }
            if (any) addToOutputs (outputs, numOutputs, i, mix * trem * pv[ElectricPianoParams::level] * 0.5f);
        }
    }
private:
    std::array<Voice, maxVoices> voices;
    std::uint32_t counter = 0;
    double tremPhase = 0.0;
};

//==============================================================================
// Mono bass: last-note priority, glide, oscillator + sub, filter envelope, drive

class BassSynth final : public Instrument
{
public:
    BassSynth() : Instrument (InstrumentType::bass) {}
    void reset() override { active = false; env.kill(); heldNotes.clear(); }

    void noteOn (int pitch, float newVelocity, float newGain, int delaySamples, int gateSamples, const InstrumentParams&) noexcept override
    {
        if (! juce::isPositiveAndBelow (pitch, 128)) return;
        if (heldNotes.size() < heldNotes.capacity()) heldNotes.push_back (pitch);
        targetFreq = midiToHz (pitch);
        if (! active) { currentFreq = targetFreq; phase = 0.0; subPhase = 0.0; filterEnv = 1.0f; env.start(); filter.reset(); }
        else filterEnv = 1.0f;    // retrigger the filter envelope legato
        active = true; velocity = juce::jlimit (0.0f, 1.0f, newVelocity); gain = newGain;
        delay = juce::jmax (0, delaySamples); gate = gateSamples; currentPitch = pitch;
    }
    void noteOff (int pitch) noexcept override
    {
        heldNotes.erase (pitch);
        if (! heldNotes.empty()) { targetFreq = midiToHz (heldNotes.back()); currentPitch = heldNotes.back(); }
        else if (pitch == currentPitch && gate < 0) gate = 0;
    }
    void allNotesOff (bool immediate) noexcept override
    {
        heldNotes.clear();
        if (immediate) { active = false; env.kill(); }
        else if (active && env.stage != Adsr::Stage::release) gate = 0;
    }
    int getNumActiveVoices() const noexcept override { return active ? 1 : 0; }

    void render (float* const* outputs, int numOutputs, int n, const InstrumentParams& p) noexcept override
    {
        if (! active) return;
        const auto& pv = p.values;
        const auto wave = pv[BassParams::wave] >= 0.5f ? Wave::square : Wave::saw;
        const float glideCoef = (float) std::exp (-1.0 / juce::jmax (1.0, pv[BassParams::glide] * sampleRate));
        const float fEnvCoef = (float) std::exp (-1.0 / juce::jmax (1.0, pv[BassParams::decay] * sampleRate));
        const float driveGain = juce::Decibels::decibelsToGain (pv[BassParams::drive]);
        const int offset = juce::jmin (delay, n); delay -= offset;
        for (int i = offset; i < n; ++i)
        {
            if (gate == 0 && env.stage != Adsr::Stage::release) { env.release (0.08, sampleRate); gate = -1; }
            else if (gate > 0) --gate;
            const float e = env.next (0.003, 0.2, 1.0f, sampleRate);
            if (! env.isActive()) { active = false; break; }
            currentFreq += (targetFreq - currentFreq) * (1.0f - glideCoef);
            filterEnv *= fEnvCoef;
            if (((i - offset) & 15) == 0)
                filter.setCutoff (pv[BassParams::cutoff] * std::pow (2.0, pv[BassParams::filterEnv] * filterEnv), pv[BassParams::resonance], sampleRate);
            const double inc = currentFreq / sampleRate;
            float osc = oscillator (wave, phase, inc) + pv[BassParams::sub] * (float) std::sin (juce::MathConstants<double>::twoPi * subPhase);
            phase += inc; if (phase >= 1.0) phase -= 1.0;
            subPhase += inc * 0.5; if (subPhase >= 1.0) subPhase -= 1.0;
            float x = filter.process (osc);
            x = std::tanh (x * driveGain) / juce::jmax (1.0f, std::tanh (driveGain));
            addToOutputs (outputs, numOutputs, i, x * e * velocity * gain * pv[BassParams::level]);
        }
    }
private:
    bool active = false;
    int currentPitch = 0, delay = 0, gate = -1;
    float velocity = 0.0f, gain = 1.0f, filterEnv = 1.0f;
    double phase = 0.0, subPhase = 0.0, currentFreq = 110.0, targetFreq = 110.0;
    Adsr env;
    Svf filter;
    struct HeldNotes { std::array<int, 16> notes {}; int count = 0;
        size_t size() const { return (size_t) count; } size_t capacity() const { return 16; } bool empty() const { return count == 0; }
        void push_back (int n) { notes[(size_t) count++] = n; } int back() const { return notes[(size_t) count - 1]; } void clear() { count = 0; }
        void erase (int n) { int w = 0; for (int i = 0; i < count; ++i) if (notes[(size_t) i] != n) notes[(size_t) w++] = notes[(size_t) i]; count = w; }
    } heldNotes;
};

//==============================================================================

std::unique_ptr<Instrument> Instrument::create (InstrumentType t, double sr)
{
    std::unique_ptr<Instrument> inst;
    switch (t)
    {
        case InstrumentType::subtractive:   inst = std::make_unique<SubtractiveSynth>(); break;
        case InstrumentType::fm:            inst = std::make_unique<FmSynth>(); break;
        case InstrumentType::wavetable:     inst = std::make_unique<WavetableSynth>(); break;
        case InstrumentType::sampler:       inst = std::make_unique<Sampler>(); break;
        case InstrumentType::electricPiano: inst = std::make_unique<ElectricPiano>(); break;
        case InstrumentType::bass:          inst = std::make_unique<BassSynth>(); break;
        case InstrumentType::none:          return nullptr;
    }
    inst->prepare (sr);
    return inst;
}

} // namespace beatmaker::engine
