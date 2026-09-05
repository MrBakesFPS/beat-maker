#include "Effects.h"
#include <cmath>

namespace beatmaker::engine
{

//==============================================================================
// Factory & metadata

const std::vector<EffectType>& Effect::availableTypes()
{
    static const std::vector<EffectType> types { EffectType::eq, EffectType::compressor, EffectType::delay, EffectType::reverb };
    return types;
}

const char* Effect::typeName (EffectType t)
{
    switch (t)
    {
        case EffectType::none:       return "No Insert";
        case EffectType::eq:         return "EQ";
        case EffectType::compressor: return "Compressor";
        case EffectType::delay:      return "Delay";
        case EffectType::reverb:     return "Reverb";
    }
    return "";
}

const std::vector<ParamInfo>& Effect::paramInfo (EffectType t)
{
    static const std::vector<ParamInfo> none;
    static const std::vector<ParamInfo> eq {
        { "Low",      -18.0f, 18.0f,    0.0f,    0.0f,    " dB" },
        { "Low Hz",    30.0f, 500.0f,  120.0f,  120.0f,   " Hz" },
        { "Mid",      -18.0f, 18.0f,    0.0f,    0.0f,    " dB" },
        { "Mid Hz",   200.0f, 8000.0f, 1000.0f, 1000.0f,  " Hz" },
        { "Mid Q",      0.3f, 8.0f,     1.0f,    1.0f,    "" },
        { "High",     -18.0f, 18.0f,    0.0f,    0.0f,    " dB" },
        { "High Hz", 2000.0f, 16000.0f, 8000.0f, 6000.0f, " Hz" } };
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
        { "High Cut", 500.0f, 20000.0f, 6000.0f, 4000.0f, " Hz" } };
    static const std::vector<ParamInfo> reverb {
        { "Room",       0.0f, 100.0f,  60.0f,  0.0f, " %" },
        { "Damping",    0.0f, 100.0f,  50.0f,  0.0f, " %" },
        { "Width",      0.0f, 100.0f, 100.0f,  0.0f, " %" },
        { "Mix",        0.0f, 100.0f,  25.0f,  0.0f, " %" } };

    switch (t)
    {
        case EffectType::eq:         return eq;
        case EffectType::compressor: return comp;
        case EffectType::delay:      return delay;
        case EffectType::reverb:     return reverb;
        case EffectType::none:       break;
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
        case EffectType::delay:      fx = std::make_unique<DelayEffect>(); break;
        case EffectType::reverb:     fx = std::make_unique<ReverbEffect>(); break;
        case EffectType::none:       return nullptr;
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

Biquad::Coefficients Biquad::highPass (double sr, double freq, double q) noexcept
{
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (1.0, sr * 0.45, freq) / sr;
    const double cosw = std::cos (w0), sinw = std::sin (w0);
    const double alpha = sinw / (2.0 * q);
    const double b0 = (1 + cosw) / 2, b1 = -(1 + cosw), b2 = (1 + cosw) / 2;
    const double a0 = 1 + alpha, a1 = -2 * cosw, a2 = 1 - alpha;
    return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
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
// EQ

void EqEffect::reset()
{
    for (auto& s : lowState) s = {};
    for (auto& s : midState) s = {};
    for (auto& s : highState) s = {};
    coefficientsValid = false;
}

void EqEffect::process (juce::AudioBuffer<float>& buffer, int n, const InsertParams& p) noexcept
{
    if (! coefficientsValid || p.values != cached)
    {
        cached = p.values;
        low  = Biquad::lowShelf  (sampleRate, cached[lowFreq],  cached[lowGain]);
        mid  = Biquad::peak      (sampleRate, cached[midFreq],  cached[midGain], cached[midQ]);
        high = Biquad::highShelf (sampleRate, cached[highFreq], cached[highGain]);
        coefficientsValid = true;
    }

    const bool lowFlat = std::abs (cached[lowGain]) < 0.01f, midFlat = std::abs (cached[midGain]) < 0.01f, highFlat = std::abs (cached[highGain]) < 0.01f;
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        if (! lowFlat)  Biquad::process (low,  lowState[(size_t) ch],  d, n);
        if (! midFlat)  Biquad::process (mid,  midState[(size_t) ch],  d, n);
        if (! highFlat) Biquad::process (high, highState[(size_t) ch], d, n);
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

    for (int i = 0; i < n; ++i)
    {
        const int readPos = (writePos - delaySamples + len) % len;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& line = lines[(size_t) ch];
            const float in = buffer.getSample (ch, i);
            const float delayed = line[(size_t) readPos];
            lpState[(size_t) ch] = delayed + lpCoef * (lpState[(size_t) ch] - delayed);   // one-pole low-pass in the loop
            line[(size_t) writePos] = in + lpState[(size_t) ch] * fb;
            buffer.setSample (ch, i, in * dry + delayed * wet);
        }
        writePos = (writePos + 1) % len;
    }
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
