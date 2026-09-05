#include "Effects.h"
#include <cmath>
#include <complex>

namespace beatmaker::engine
{

//==============================================================================
// Factory & metadata

const std::vector<EffectType>& Effect::availableTypes()
{
    static const std::vector<EffectType> types { EffectType::eq, EffectType::compressor, EffectType::limiter, EffectType::gate, EffectType::deesser,
                                                 EffectType::delay, EffectType::reverb, EffectType::chorus, EffectType::flanger, EffectType::phaser,
                                                 EffectType::saturation, EffectType::ampSim, EffectType::utility };
    return types;
}

const char* Effect::typeName (EffectType t)
{
    switch (t)
    {
        case EffectType::none:       return "No Insert";
        case EffectType::eq:         return "EQ";
        case EffectType::compressor: return "Compressor";
        case EffectType::limiter:    return "Limiter";
        case EffectType::gate:       return "Gate/Expander";
        case EffectType::deesser:    return "De-esser";
        case EffectType::delay:      return "Delay";
        case EffectType::reverb:     return "Reverb";
        case EffectType::chorus:     return "Chorus";
        case EffectType::flanger:    return "Flanger";
        case EffectType::phaser:     return "Phaser";
        case EffectType::saturation: return "Saturation";
        case EffectType::ampSim:     return "Amp Sim";
        case EffectType::utility:    return "Utility";
        case EffectType::plugin:     return "Plugin";
    }
    return "";
}

const std::vector<ParamInfo>& Effect::paramInfo (EffectType t)
{
    static const std::vector<ParamInfo> none;
    static const std::vector<ParamInfo> eq {
        { "HPF",       10.0f, 1000.0f,  10.0f,   80.0f,    " Hz" },   // 10 = off
        { "Low",      -18.0f, 18.0f,    0.0f,    0.0f,    " dB" },
        { "Low Hz",    30.0f, 500.0f,  120.0f,  120.0f,   " Hz" },
        { "LMF",      -18.0f, 18.0f,    0.0f,    0.0f,    " dB" },
        { "LMF Hz",    80.0f, 2000.0f, 300.0f,  300.0f,   " Hz" },
        { "LMF Q",      0.3f, 8.0f,     1.0f,    1.0f,    "" },
        { "MF",       -18.0f, 18.0f,    0.0f,    0.0f,    " dB" },
        { "MF Hz",    200.0f, 8000.0f, 1000.0f, 1000.0f,  " Hz" },
        { "MF Q",       0.3f, 8.0f,     1.0f,    1.0f,    "" },
        { "HMF",      -18.0f, 18.0f,    0.0f,    0.0f,    " dB" },
        { "HMF Hz",   800.0f, 12000.0f, 3000.0f, 3000.0f, " Hz" },
        { "HMF Q",      0.3f, 8.0f,     1.0f,    1.0f,    "" },
        { "High",     -18.0f, 18.0f,    0.0f,    0.0f,    " dB" },
        { "High Hz", 2000.0f, 16000.0f, 8000.0f, 6000.0f, " Hz" },
        { "LPF",     1000.0f, 20000.0f, 20000.0f, 8000.0f, " Hz" } };   // 20000 = off
    static const std::vector<ParamInfo> limiter {
        { "Ceiling",  -20.0f, 0.0f,   -0.3f,  0.0f,   " dB" },
        { "Release",    5.0f, 1000.0f, 80.0f, 100.0f, " ms" },
        { "Input",    -12.0f, 24.0f,    0.0f,  0.0f,   " dB" } };
    static const std::vector<ParamInfo> gate {
        { "Thresh",   -80.0f, 0.0f,   -40.0f,  0.0f,  " dB" },
        { "Ratio",      1.0f, 100.0f, 100.0f, 10.0f, ":1" },
        { "Attack",     0.1f, 100.0f,   1.0f,  5.0f,  " ms" },
        { "Hold",       0.0f, 500.0f,  20.0f, 50.0f,  " ms" },
        { "Release",    5.0f, 2000.0f, 100.0f, 200.0f, " ms" },
        { "Range",    -90.0f, 0.0f,   -90.0f,  0.0f,  " dB" } };
    static const std::vector<ParamInfo> deesser {
        { "Freq",    2000.0f, 12000.0f, 6000.0f, 6000.0f, " Hz" },
        { "Thresh",   -60.0f, 0.0f,    -30.0f,  0.0f,    " dB" },
        { "Range",      0.0f, 24.0f,    12.0f,  0.0f,    " dB" },
        { "Release",    5.0f, 500.0f,   60.0f, 60.0f,    " ms" } };
    static const std::vector<ParamInfo> chorus {
        { "Rate",       0.05f, 5.0f,   0.8f,  0.8f,  " Hz" },
        { "Depth",      0.0f, 100.0f,  40.0f, 0.0f,  " %" },
        { "Feedback",   0.0f, 60.0f,    0.0f, 0.0f,  " %" },
        { "Mix",        0.0f, 100.0f,  50.0f, 0.0f,  " %" },
        { "Voices",     1.0f, 3.0f,     2.0f, 0.0f,  "" } };
    static const std::vector<ParamInfo> flanger {
        { "Rate",       0.05f, 5.0f,   0.3f,  0.5f,  " Hz" },
        { "Depth",      0.0f, 100.0f,  70.0f, 0.0f,  " %" },
        { "Feedback",   0.0f, 90.0f,   50.0f, 0.0f,  " %" },
        { "Mix",        0.0f, 100.0f,  50.0f, 0.0f,  " %" } };
    static const std::vector<ParamInfo> phaser {
        { "Rate",       0.05f, 5.0f,   0.4f,  0.5f,  " Hz" },
        { "Depth",      0.0f, 100.0f,  80.0f, 0.0f,  " %" },
        { "Stages",     2.0f, 8.0f,     4.0f, 0.0f,  "" },
        { "Feedback",   0.0f, 90.0f,   30.0f, 0.0f,  " %" },
        { "Mix",        0.0f, 100.0f,  50.0f, 0.0f,  " %" } };
    static const std::vector<ParamInfo> saturation {
        { "Drive",      0.0f, 36.0f,   12.0f,  12.0f, " dB" },
        { "Type",       0.0f, 2.0f,     0.0f,  0.0f,  "" },      // 0 soft, 1 hard, 2 tube
        { "Tone",    1000.0f, 20000.0f, 8000.0f, 5000.0f, " Hz" },
        { "Output",   -24.0f, 12.0f,   -6.0f,  0.0f,  " dB" } };
    static const std::vector<ParamInfo> amp {
        { "Gain",       0.0f, 40.0f,   18.0f,  15.0f, " dB" },
        { "Bass",     -12.0f, 12.0f,    0.0f,  0.0f,  " dB" },
        { "Mid",      -12.0f, 12.0f,    0.0f,  0.0f,  " dB" },
        { "Treble",   -12.0f, 12.0f,    0.0f,  0.0f,  " dB" },
        { "Presence", -12.0f, 12.0f,    0.0f,  0.0f,  " dB" },
        { "Cabinet",    0.0f, 1.0f,     1.0f,  0.0f,  "" },
        { "Master",   -24.0f, 6.0f,   -10.0f,  0.0f,  " dB" } };
    static const std::vector<ParamInfo> utility {
        { "Gain",     -60.0f, 12.0f,    0.0f,  0.0f,  " dB" },
        { "Phase L",    0.0f, 1.0f,     0.0f,  0.0f,  "" },
        { "Phase R",    0.0f, 1.0f,     0.0f,  0.0f,  "" },
        { "Width",      0.0f, 200.0f, 100.0f,  0.0f,  " %" },
        { "Mono",       0.0f, 1.0f,     0.0f,  0.0f,  "" } };
    static const std::vector<ParamInfo> comp {
        { "Thresh",   -60.0f, 0.0f,   -18.0f,  0.0f,  " dB" },
        { "Ratio",      1.0f, 20.0f,    4.0f,  4.0f,  ":1" },
        { "Attack",     0.1f, 200.0f,  10.0f, 20.0f,  " ms" },
        { "Release",    5.0f, 2000.0f, 120.0f, 200.0f, " ms" },
        { "Makeup",     0.0f, 24.0f,    0.0f,  0.0f,  " dB" },
        { "Lookahead",  0.0f, 10.0f,    0.0f,  0.0f,  " ms" } };
    static const std::vector<ParamInfo> delay {
        { "Time",       1.0f, 2000.0f, 375.0f, 300.0f, " ms" },
        { "Feedback",   0.0f, 95.0f,   35.0f,  0.0f,  " %" },
        { "Mix",        0.0f, 100.0f,  30.0f,  0.0f,  " %" },
        { "High Cut", 500.0f, 20000.0f, 6000.0f, 4000.0f, " Hz" },
        { "Mode",       0.0f, 2.0f,     0.0f,  0.0f,  "" } };   // 0 digital, 1 tape, 2 ping-pong
    static const std::vector<ParamInfo> reverb {
        { "Room",       0.0f, 100.0f,  60.0f,  0.0f, " %" },
        { "Damping",    0.0f, 100.0f,  50.0f,  0.0f, " %" },
        { "Width",      0.0f, 100.0f, 100.0f,  0.0f, " %" },
        { "Mix",        0.0f, 100.0f,  25.0f,  0.0f, " %" } };

    switch (t)
    {
        case EffectType::eq:         return eq;
        case EffectType::compressor: return comp;
        case EffectType::limiter:    return limiter;
        case EffectType::gate:       return gate;
        case EffectType::deesser:    return deesser;
        case EffectType::delay:      return delay;
        case EffectType::reverb:     return reverb;
        case EffectType::chorus:     return chorus;
        case EffectType::flanger:    return flanger;
        case EffectType::phaser:     return phaser;
        case EffectType::saturation: return saturation;
        case EffectType::ampSim:     return amp;
        case EffectType::utility:    return utility;
        case EffectType::none:
        case EffectType::plugin:     break;
    }
    return none;
}

InsertParams Effect::defaultParams (EffectType t)
{
    InsertParams p;
    p.type = t;
    const auto& info = paramInfo (t);
    for (size_t i = 0; i < info.size() && i < p.values.size(); ++i)
        p.values[i] = info[i].def;
    return p;
}

std::unique_ptr<Effect> Effect::create (EffectType t, double sampleRate, int maxBlockSize)
{
    std::unique_ptr<Effect> fx;
    switch (t)
    {
        case EffectType::eq:         fx = std::make_unique<EqEffect>(); break;
        case EffectType::compressor: fx = std::make_unique<CompressorEffect>(); break;
        case EffectType::limiter:    fx = std::make_unique<LimiterEffect>(); break;
        case EffectType::gate:       fx = std::make_unique<GateEffect>(); break;
        case EffectType::deesser:    fx = std::make_unique<DeEsserEffect>(); break;
        case EffectType::delay:      fx = std::make_unique<DelayEffect>(); break;
        case EffectType::reverb:     fx = std::make_unique<ReverbEffect>(); break;
        case EffectType::chorus:     fx = std::make_unique<ModulatedDelayEffect> (EffectType::chorus); break;
        case EffectType::flanger:    fx = std::make_unique<ModulatedDelayEffect> (EffectType::flanger); break;
        case EffectType::phaser:     fx = std::make_unique<PhaserEffect>(); break;
        case EffectType::saturation: fx = std::make_unique<SaturationEffect>(); break;
        case EffectType::ampSim:     fx = std::make_unique<AmpSimEffect>(); break;
        case EffectType::utility:    fx = std::make_unique<UtilityEffect>(); break;
        case EffectType::none:
        case EffectType::plugin:     return nullptr;   // plugins are created by the PluginManager
    }
    fx->prepare (sampleRate, maxBlockSize);
    return fx;
}

//==============================================================================
// Biquad

Biquad::Coefficients Biquad::lowShelf (double sr, double freq, double gainDb, double slope) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, freq) / sr;
    const double cosw = std::cos (w0), sinw = std::sin (w0);
    const double alpha = sinw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double sq = 2.0 * std::sqrt (A) * alpha;
    const double b0 = A * ((A + 1) - (A - 1) * cosw + sq), b1 = 2 * A * ((A - 1) - (A + 1) * cosw), b2 = A * ((A + 1) - (A - 1) * cosw - sq);
    const double a0 = (A + 1) + (A - 1) * cosw + sq, a1 = -2 * ((A - 1) + (A + 1) * cosw), a2 = (A + 1) + (A - 1) * cosw - sq;
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
}

Biquad::Coefficients Biquad::highShelf (double sr, double freq, double gainDb, double slope) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, freq) / sr;
    const double cosw = std::cos (w0), sinw = std::sin (w0);
    const double alpha = sinw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double sq = 2.0 * std::sqrt (A) * alpha;
    const double b0 = A * ((A + 1) + (A - 1) * cosw + sq), b1 = -2 * A * ((A - 1) + (A + 1) * cosw), b2 = A * ((A + 1) + (A - 1) * cosw - sq);
    const double a0 = (A + 1) - (A - 1) * cosw + sq, a1 = 2 * ((A - 1) - (A + 1) * cosw), a2 = (A + 1) - (A - 1) * cosw - sq;
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
}

Biquad::Coefficients Biquad::peak (double sr, double freq, double gainDb, double q) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, freq) / sr;
    const double cosw = std::cos (w0), sinw = std::sin (w0);
    const double alpha = sinw / (2.0 * juce::jmax (0.05, q));
    const double b0 = 1 + alpha * A, b1 = -2 * cosw, b2 = 1 - alpha * A;
    const double a0 = 1 + alpha / A, a1 = -2 * cosw, a2 = 1 - alpha / A;
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
}

Biquad::Coefficients Biquad::highShelfQ (double sr, double freq, double gainDb, double q) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, freq) / sr;
    const double cosw = std::cos (w0), sinw = std::sin (w0);
    const double alpha = sinw / (2.0 * q);
    const double sq = 2.0 * std::sqrt (A) * alpha;
    const double b0 = A * ((A + 1) + (A - 1) * cosw + sq), b1 = -2 * A * ((A - 1) + (A + 1) * cosw), b2 = A * ((A + 1) + (A - 1) * cosw - sq);
    const double a0 = (A + 1) - (A - 1) * cosw + sq, a1 = 2 * ((A - 1) - (A + 1) * cosw), a2 = (A + 1) - (A - 1) * cosw - sq;
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
}

Biquad::Coefficients Biquad::lowPass (double sr, double freq, double q) noexcept
{
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, sr * 0.45, freq) / sr;
    const double cosw = std::cos (w0), sinw = std::sin (w0);
    const double alpha = sinw / (2.0 * q);
    const double b0 = (1 - cosw) / 2, b1 = 1 - cosw, b2 = (1 - cosw) / 2;
    const double a0 = 1 + alpha, a1 = -2 * cosw, a2 = 1 - alpha;
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
}

Biquad::Coefficients Biquad::highPass (double sr, double freq, double q) noexcept
{
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, sr * 0.45, freq) / sr;
    const double cosw = std::cos (w0), sinw = std::sin (w0);
    const double alpha = sinw / (2.0 * q);
    const double b0 = (1 + cosw) / 2, b1 = -(1 + cosw), b2 = (1 + cosw) / 2;
    const double a0 = 1 + alpha, a1 = -2 * cosw, a2 = 1 - alpha;
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
}

double Biquad::magnitudeDb (const Coefficients& c, double sr, double freq) noexcept
{
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const std::complex<double> num = (double) c.b0 + (double) c.b1 * z1 + (double) c.b2 * z2;
    const std::complex<double> den = 1.0 + (double) c.a1 * z1 + (double) c.a2 * z2;
    const double mag = std::abs (num / den);
    return mag > 1.0e-9 ? 20.0 * std::log10 (mag) : -180.0;
}

void Biquad::process (const Coefficients& c, State& s, float* d, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float x = d[i];
        const float y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        d[i] = y;
    }
}

//==============================================================================
// EQ (7-band)

namespace
{
    Biquad::Coefficients passThrough() { return {}; }
}

std::array<bool, EqEffect::numBands> EqEffect::activeBands (const InsertParams& p) noexcept
{
    const auto& v = p.values;
    return { v[hpFreq] > 10.5f, std::abs (v[lowGain]) >= 0.01f, std::abs (v[lmGain]) >= 0.01f, std::abs (v[midGain]) >= 0.01f,
             std::abs (v[hmGain]) >= 0.01f, std::abs (v[highGain]) >= 0.01f, v[lpFreq] < 19999.0f };
}

std::array<Biquad::Coefficients, EqEffect::numBands> EqEffect::coefficientsFor (const InsertParams& p, double sr) noexcept
{
    const auto& v = p.values;
    const auto on = activeBands (p);
    return { on[0] ? Biquad::highPass (sr, v[hpFreq], 0.7071) : passThrough(),
             on[1] ? Biquad::lowShelf (sr, v[lowFreq], v[lowGain]) : passThrough(),
             on[2] ? Biquad::peak (sr, v[lmFreq], v[lmGain], v[lmQ]) : passThrough(),
             on[3] ? Biquad::peak (sr, v[midFreq], v[midGain], v[midQ]) : passThrough(),
             on[4] ? Biquad::peak (sr, v[hmFreq], v[hmGain], v[hmQ]) : passThrough(),
             on[5] ? Biquad::highShelf (sr, v[highFreq], v[highGain]) : passThrough(),
             on[6] ? Biquad::lowPass (sr, v[lpFreq], 0.7071) : passThrough() };
}

void EqEffect::reset()
{
    for (auto& band : state) for (auto& st : band) st = {};
    coefficientsValid = false;
}

void EqEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    if (! coefficientsValid || p.values != cached)
    {
        cached = p.values;
        bands = coefficientsFor (p, sampleRate);
        active = activeBands (p);
        coefficientsValid = true;
    }

    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int b = 0; b < numBands; ++b)
            if (active[(size_t) b]) Biquad::process (bands[(size_t) b], state[(size_t) b][(size_t) ch], d, n);
    }
}

//==============================================================================
// Compressor

void CompressorEffect::prepareImpl (int)
{
    const auto len = (size_t) std::ceil (maxLookaheadMs * 0.001 * sampleRate) + 1;
    for (auto& l : lookaheadLines) l.assign (len, 0.0f);
}

void CompressorEffect::reset()
{
    envelope = 0.0f;
    gainReductionDb.store (0.0f);
    for (auto& l : lookaheadLines) std::fill (l.begin(), l.end(), 0.0f);
    lookaheadWrite = 0;
}

int CompressorEffect::getLatencySamples (const InsertParams& p) const noexcept
{
    return (int) std::lround (juce::jlimit (0.0f, maxLookaheadMs, p.values[lookahead]) * 0.001 * sampleRate);
}

void CompressorEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    const int lookaheadSamples = getLatencySamples (p);
    const int lineLen = (int) lookaheadLines[0].size();
    const float thresholdDb = p.values[threshold];
    const float ratioValue = juce::jmax (1.0f, p.values[ratio]);
    const float att = std::exp (-1.0f / (float) (juce::jmax (0.01f, p.values[attack]) * 0.001 * sampleRate));
    const float rel = std::exp (-1.0f / (float) (juce::jmax (1.0f, p.values[release]) * 0.001 * sampleRate));
    const float makeupGain = juce::Decibels::decibelsToGain (p.values[makeup]);
    const int channels = juce::jmin (2, buffer.getNumChannels());
    float maxReduction = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float level = 0.0f;
        for (int ch = 0; ch < channels; ++ch) level = juce::jmax (level, std::abs (buffer.getSample (ch, i)));

        envelope = level > envelope ? att * envelope + (1.0f - att) * level : rel * envelope + (1.0f - rel) * level;
        const float envDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
        const float over = envDb - thresholdDb;
        const float reductionDb = over > 0.0f ? over * (1.0f - 1.0f / ratioValue) : 0.0f;
        maxReduction = juce::jmax (maxReduction, reductionDb);
        const float g = juce::Decibels::decibelsToGain (-reductionDb) * makeupGain;

        if (lookaheadSamples > 0 && lineLen > lookaheadSamples)
        {
            const int readPos = (lookaheadWrite - lookaheadSamples + lineLen) % lineLen;
            for (int ch = 0; ch < channels; ++ch)
            {
                auto& line = lookaheadLines[(size_t) ch];
                const float delayed = line[(size_t) readPos];
                line[(size_t) lookaheadWrite] = buffer.getSample (ch, i);
                buffer.setSample (ch, i, delayed * g);
            }
            lookaheadWrite = (lookaheadWrite + 1) % lineLen;
        }
        else
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
    gainReductionDb.store (maxReduction, std::memory_order_relaxed);
}

//==============================================================================
// Delay

void DelayEffect::prepareImpl (int)
{
    const auto len = (size_t) std::ceil (maxDelaySeconds * sampleRate) + 1;
    for (auto& l : lines) l.assign (len, 0.0f);
}

void DelayEffect::reset()
{
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0;
    lpState = {};
}

void DelayEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    const int len = (int) lines[0].size();
    if (len == 0) return;
    const int delaySamples = juce::jlimit (1, len - 1, (int) std::lround (p.values[time] * 0.001 * sampleRate));
    const float fb = juce::jlimit (0.0f, 0.95f, p.values[feedback] * 0.01f);
    const float wet = juce::jlimit (0.0f, 1.0f, p.values[mix] * 0.01f);
    const float dry = 1.0f - wet;
    const float lpCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jlimit (100.0f, 20000.0f, p.values[highCut]) / sampleRate);
    const int channels = juce::jmin (2, buffer.getNumChannels());
    const int modeValue = (int) std::lround (juce::jlimit (0.0f, 2.0f, p.values[mode]));
    const bool tape = modeValue == 1, pingPong = modeValue == 2 && channels == 2;

    for (int i = 0; i < n; ++i)
    {
        const int readPos = (writePos - delaySamples + len) % len;
        float delayed[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < channels; ++ch) delayed[ch] = lines[(size_t) ch][(size_t) readPos];

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& line = lines[(size_t) ch];
            const float in = buffer.getSample (ch, i);
            // Feedback source: own channel, or the opposite one for ping-pong
            const float fbSrc = pingPong ? delayed[1 - ch] : delayed[ch];
            lpState[(size_t) ch] = fbSrc + lpCoef * (lpState[(size_t) ch] - fbSrc);   // one-pole low-pass in the loop
            float loop = lpState[(size_t) ch] * fb;
            if (tape) loop = std::tanh (loop * 1.5f) / 1.5f;                            // gentle tape saturation
            // Ping-pong: a mono-summed input enters the left line only, then bounces
            const float feed = pingPong ? (ch == 0 ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)) : 0.0f) : in;
            line[(size_t) writePos] = feed + loop;
            buffer.setSample (ch, i, in * dry + delayed[ch] * wet);
        }
        writePos = (writePos + 1) % len;
    }
}

//==============================================================================
// Limiter

void LimiterEffect::prepareImpl (int)
{
    lookaheadSamples = (int) std::lround (lookaheadMs * 0.001 * sampleRate);
    for (auto& l : lines) l.assign ((size_t) lookaheadSamples + 1, 0.0f);
}

void LimiterEffect::reset()
{
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0; envelope = 0.0f; gainReductionDb.store (0.0f);
}

void LimiterEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    const float ceilingGain = juce::Decibels::decibelsToGain (p.values[ceiling]);
    const float inGain = juce::Decibels::decibelsToGain (p.values[inputGain]);
    const float rel = std::exp (-1.0f / (float) (juce::jmax (1.0f, p.values[release]) * 0.001 * sampleRate));
    const int channels = juce::jmin (2, buffer.getNumChannels());
    const int len = (int) lines[0].size();
    float maxReduction = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // Peak of the incoming sample (pre-delay) drives the envelope: with the
        // signal delayed by the lookahead, the gain is already down when the peak arrives.
        float level = 0.0f;
        for (int ch = 0; ch < channels; ++ch) level = juce::jmax (level, std::abs (buffer.getSample (ch, i) * inGain));
        envelope = level > envelope ? level : rel * envelope + (1.0f - rel) * level;   // instant attack
        const float g = envelope > ceilingGain ? ceilingGain / envelope : 1.0f;
        maxReduction = juce::jmax (maxReduction, juce::Decibels::gainToDecibels (1.0f / g));

        const int readPos = (writePos - lookaheadSamples + len) % len;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& line = lines[(size_t) ch];
            const float delayed = line[(size_t) readPos];
            line[(size_t) writePos] = buffer.getSample (ch, i) * inGain;
            buffer.setSample (ch, i, juce::jlimit (-ceilingGain, ceilingGain, delayed * g));   // hard safety clip at the ceiling
        }
        writePos = (writePos + 1) % len;
    }
    gainReductionDb.store (maxReduction, std::memory_order_relaxed);
}

//==============================================================================
// Gate / Expander

void GateEffect::reset() { envelope = 0.0f; gain = 1.0f; holdCounter = 0; gainReductionDb.store (0.0f); }

void GateEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    const float thresholdDb = p.values[threshold];
    const float ratioValue = juce::jmax (1.0f, p.values[ratio]);
    const float rangeDb = p.values[range];
    const float att = std::exp (-1.0f / (float) (juce::jmax (0.01f, p.values[attack]) * 0.001 * sampleRate));
    const float rel = std::exp (-1.0f / (float) (juce::jmax (1.0f, p.values[release]) * 0.001 * sampleRate));
    const int holdSamples = (int) (p.values[hold] * 0.001 * sampleRate);
    const int channels = juce::jmin (2, buffer.getNumChannels());
    float maxReduction = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float level = 0.0f;
        for (int ch = 0; ch < channels; ++ch) level = juce::jmax (level, std::abs (buffer.getSample (ch, i)));
        // Fast detector so the gate opens quickly
        envelope = level > envelope ? 0.3f * envelope + 0.7f * level : 0.999f * envelope + 0.001f * level;
        const float envDb = juce::Decibels::gainToDecibels (envelope, -120.0f);

        float targetDb = 0.0f;
        if (envDb < thresholdDb)
        {
            const float below = thresholdDb - envDb;
            targetDb = -juce::jmin (below * (ratioValue - 1.0f), -rangeDb);   // expander: reduce by (ratio-1) x amount below, capped at range
        }
        const float targetGain = juce::Decibels::decibelsToGain (targetDb);

        if (targetGain >= gain) { gain = att * gain + (1.0f - att) * targetGain; holdCounter = holdSamples; }   // opening
        else if (holdCounter > 0) --holdCounter;                                                                // holding
        else gain = rel * gain + (1.0f - rel) * targetGain;                                                     // closing

        maxReduction = juce::jmax (maxReduction, -juce::Decibels::gainToDecibels (gain, -120.0f));
        for (int ch = 0; ch < channels; ++ch) buffer.setSample (ch, i, buffer.getSample (ch, i) * gain);
    }
    gainReductionDb.store (maxReduction, std::memory_order_relaxed);
}

//==============================================================================
// De-esser

void DeEsserEffect::reset()
{
    for (auto& st : hpState) st = {};
    for (auto& st : shelfState) st = {};
    envelope = 0.0f; cachedFreq = -1.0f; cachedShelfDb = 1.0e9f; gainReductionDb.store (0.0f);
}

void DeEsserEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    if (std::abs (p.values[frequency] - cachedFreq) > 0.5f)
    {
        cachedFreq = p.values[frequency];
        hp = Biquad::highPass (sampleRate, cachedFreq, 0.7071);
        cachedShelfDb = 1.0e9f;
    }
    const float thresholdDb = p.values[threshold], rangeDb = p.values[range];
    const float rel = std::exp (-1.0f / (float) (juce::jmax (1.0f, p.values[release]) * 0.001 * sampleRate));
    const int channels = juce::jmin (2, buffer.getNumChannels());
    float maxReduction = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // Detector: level of the band above the frequency
        float level = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            float x = buffer.getSample (ch, i);
            Biquad::process (hp, hpState[(size_t) ch], &x, 1);
            level = juce::jmax (level, std::abs (x));
        }
        envelope = level > envelope ? level : rel * envelope + (1.0f - rel) * level;
        const float over = juce::Decibels::gainToDecibels (envelope, -120.0f) - thresholdDb;
        const float reductionDb = over > 0.0f ? juce::jmin (over, rangeDb) : 0.0f;
        maxReduction = juce::jmax (maxReduction, reductionDb);

        // Gain: a high shelf cut of -reductionDb at the detector frequency (cheap to refresh)
        if (std::abs (-reductionDb - cachedShelfDb) > 0.05f)
        {
            cachedShelfDb = -reductionDb;
            shelf = Biquad::highShelfQ (sampleRate, cachedFreq * 0.7, cachedShelfDb, 0.7071);
        }
        if (cachedShelfDb < -0.01f)
            for (int ch = 0; ch < channels; ++ch)
            {
                float x = buffer.getSample (ch, i);
                Biquad::process (shelf, shelfState[(size_t) ch], &x, 1);
                buffer.setSample (ch, i, x);
            }
    }
    gainReductionDb.store (maxReduction, std::memory_order_relaxed);
}

//==============================================================================
// Chorus / Flanger (modulated delay)

void ModulatedDelayEffect::prepareImpl (int)
{
    for (auto& l : lines) l.assign ((size_t) std::ceil (0.06 * sampleRate) + 4, 0.0f);   // up to 60 ms
}

void ModulatedDelayEffect::reset()
{
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0; phase = 0.0; feedbackState = {};
}

void ModulatedDelayEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    const bool isChorus = getType() == EffectType::chorus;
    const int len = (int) lines[0].size();
    if (len == 0) return;
    const double rateHz = juce::jmax (0.01f, p.values[rate]);
    const float depthAmt = juce::jlimit (0.0f, 1.0f, p.values[depth] * 0.01f);
    const float fb = juce::jlimit (0.0f, 0.95f, p.values[feedback] * 0.01f);
    const float wet = juce::jlimit (0.0f, 1.0f, p.values[mix] * 0.01f), dry = 1.0f - wet;
    const int numVoices = isChorus ? juce::jlimit (1, 3, (int) std::lround (p.values[voices])) : 1;
    // Chorus: 7..30 ms around a 15 ms centre; flanger: 0.5..8 ms
    const double baseMs = isChorus ? 15.0 : 3.0, sweepMs = isChorus ? 8.0 * depthAmt : 2.7 * depthAmt;
    const int channels = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < n; ++i)
    {
        phase += rateHz / sampleRate;
        if (phase >= 1.0) phase -= 1.0;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto& line = lines[(size_t) ch];
            const float in = buffer.getSample (ch, i);
            line[(size_t) writePos] = in + feedbackState[(size_t) ch] * fb;

            float wetSum = 0.0f;
            for (int v = 0; v < numVoices; ++v)
            {
                const double voicePhase = phase + (double) v / numVoices + (ch == 1 ? 0.25 : 0.0);
                const double lfo = std::sin (juce::MathConstants<double>::twoPi * voicePhase);
                const double delayMs = baseMs + sweepMs * lfo;
                const double delaySamples = juce::jlimit (1.0, (double) len - 2.0, delayMs * 0.001 * sampleRate);
                const double readPos = writePos - delaySamples;
                const int r0 = ((int) std::floor (readPos) + len * 2) % len;
                const int r1 = (r0 + 1) % len;
                const float frac = (float) (readPos - std::floor (readPos));
                wetSum += line[(size_t) r0] + (line[(size_t) r1] - line[(size_t) r0]) * frac;
            }
            wetSum /= (float) numVoices;
            feedbackState[(size_t) ch] = wetSum;
            buffer.setSample (ch, i, in * dry + wetSum * wet);
        }
        writePos = (writePos + 1) % len;
    }
}

//==============================================================================
// Phaser: cascaded first-order all-passes swept by an LFO

void PhaserEffect::reset() { phase = 0.0; for (auto& c : allpassState) c = {}; feedbackState = {}; }

void PhaserEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    const double rateHz = juce::jmax (0.01f, p.values[rate]);
    const float depthAmt = juce::jlimit (0.0f, 1.0f, p.values[depth] * 0.01f);
    const int numStages = juce::jlimit (2, maxStages, (int) std::lround (p.values[stages]));
    const float fb = juce::jlimit (0.0f, 0.9f, p.values[feedback] * 0.01f);
    const float wet = juce::jlimit (0.0f, 1.0f, p.values[mix] * 0.01f), dry = 1.0f - wet;
    const int channels = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < n; ++i)
    {
        phase += rateHz / sampleRate;
        if (phase >= 1.0) phase -= 1.0;
        // Sweep 200 Hz .. 200 Hz * 2^(4*depth)
        const double lfo = 0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi * phase);
        const double freq = 200.0 * std::pow (2.0, 4.0 * depthAmt * lfo);
        const float a = (float) ((std::tan (juce::MathConstants<double>::pi * freq / sampleRate) - 1.0) / (std::tan (juce::MathConstants<double>::pi * freq / sampleRate) + 1.0));

        for (int ch = 0; ch < channels; ++ch)
        {
            const float in = buffer.getSample (ch, i);
            float x = in + feedbackState[(size_t) ch] * fb;
            auto& st = allpassState[(size_t) ch];
            for (int s = 0; s < numStages; ++s)
            {
                const float y = a * x + st[(size_t) s];
                st[(size_t) s] = x - a * y;
                x = y;
            }
            feedbackState[(size_t) ch] = x;
            buffer.setSample (ch, i, in * dry + x * wet);
        }
    }
}

//==============================================================================
// Saturation

void SaturationEffect::reset() { lpState = {}; }

void SaturationEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    const float driveGain = juce::Decibels::decibelsToGain (p.values[drive]);
    const int shape = (int) std::lround (juce::jlimit (0.0f, 2.0f, p.values[type]));
    const float lpCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jlimit (200.0f, 20000.0f, p.values[tone]) / sampleRate);
    const float outGain = juce::Decibels::decibelsToGain (p.values[output]);
    const int channels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            float x = d[i] * driveGain;
            switch (shape)
            {
                case 0:  x = std::tanh (x); break;                                       // soft
                case 1:  x = juce::jlimit (-1.0f, 1.0f, x); break;                        // hard
                default: x = x >= 0.0f ? std::tanh (x) : std::tanh (x * 0.6f) / 0.6f; break;   // tube-ish asymmetry
            }
            lpState[(size_t) ch] = x + lpCoef * (lpState[(size_t) ch] - x);
            d[i] = lpState[(size_t) ch] * outGain;
        }
    }
}

//==============================================================================
// Amp Sim: drive -> asymmetric waveshaper -> tone stack -> cabinet

void AmpSimEffect::reset()
{
    for (auto* st : { &bassS, &midS, &trebleS, &presenceS, &cabS, &cabHpS }) for (auto& s2 : *st) s2 = {};
    valid = false;
}

void AmpSimEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    if (! valid || p.values != cached)
    {
        cached = p.values;
        bassC = Biquad::lowShelf (sampleRate, 120.0, cached[bass]);
        midC = Biquad::peak (sampleRate, 800.0, cached[mid], 0.8);
        trebleC = Biquad::highShelf (sampleRate, 3000.0, cached[treble]);
        presenceC = Biquad::peak (sampleRate, 5000.0, cached[presence], 1.0);
        cabC = Biquad::lowPass (sampleRate, 4500.0, 1.2);      // 4x12-ish roll-off with a bump
        cabHpC = Biquad::highPass (sampleRate, 80.0, 0.7071);
        valid = true;
    }
    const float driveGain = juce::Decibels::decibelsToGain (cached[gain]);
    const float masterGain = juce::Decibels::decibelsToGain (cached[master]);
    const bool cab = cached[cabinet] >= 0.5f;
    const int channels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float x = d[i] * driveGain;
            d[i] = x >= 0.0f ? std::tanh (x) : std::tanh (x * 0.7f) / 0.7f;   // asymmetric clip
        }
        Biquad::process (bassC, bassS[(size_t) ch], d, n);
        Biquad::process (midC, midS[(size_t) ch], d, n);
        Biquad::process (trebleC, trebleS[(size_t) ch], d, n);
        Biquad::process (presenceC, presenceS[(size_t) ch], d, n);
        if (cab)
        {
            Biquad::process (cabC, cabS[(size_t) ch], d, n);
            Biquad::process (cabHpC, cabHpS[(size_t) ch], d, n);
        }
        juce::FloatVectorOperations::multiply (d, masterGain, n);
    }
}

//==============================================================================
// Utility

void UtilityEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    const float g = juce::Decibels::decibelsToGain (p.values[gain], -100.0f);
    const int channels = juce::jmin (2, buffer.getNumChannels());
    if (channels >= 1 && p.values[invertL] >= 0.5f) juce::FloatVectorOperations::negate (buffer.getWritePointer (0), buffer.getReadPointer (0), n);
    if (channels >= 2 && p.values[invertR] >= 0.5f) juce::FloatVectorOperations::negate (buffer.getWritePointer (1), buffer.getReadPointer (1), n);

    if (channels == 2)
    {
        auto* l = buffer.getWritePointer (0); auto* r = buffer.getWritePointer (1);
        const float widthAmt = p.values[mono] >= 0.5f ? 0.0f : juce::jlimit (0.0f, 2.0f, p.values[width] * 0.01f);
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]), s2 = 0.5f * (l[i] - r[i]) * widthAmt;
            l[i] = m + s2; r[i] = m - s2;
        }
    }
    for (int ch = 0; ch < channels; ++ch) buffer.applyGain (ch, 0, n, g);
}

//==============================================================================
// Reverb

void ReverbEffect::prepareImpl (int) { reverb.setSampleRate (sampleRate); }
void ReverbEffect::reset() { reverb.reset(); }

void ReverbEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    juce::Reverb::Parameters rp;
    rp.roomSize = juce::jlimit (0.0f, 1.0f, p.values[roomSize] * 0.01f);
    rp.damping  = juce::jlimit (0.0f, 1.0f, p.values[damping] * 0.01f);
    rp.width    = juce::jlimit (0.0f, 1.0f, p.values[width] * 0.01f);
    rp.wetLevel = juce::jlimit (0.0f, 1.0f, p.values[mix] * 0.01f);
    rp.dryLevel = 1.0f - rp.wetLevel;
    reverb.setParameters (rp);

    if (buffer.getNumChannels() >= 2)
        reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    else if (buffer.getNumChannels() == 1)
        reverb.processMono (buffer.getWritePointer (0), n);
}

} // namespace beatmaker::engine
