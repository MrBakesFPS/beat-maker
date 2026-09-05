// Small building blocks shared by the instruments: ADSR, state-variable
// filter, band-limited oscillators. Header-only, RT-safe.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace beatmaker::engine::dsp
{

struct Adsr
{
    enum class Stage { attack, decay, sustain, release, off };
    Stage stage = Stage::off;
    float level = 0.0f;
    float releaseStep = 0.0f;

    void start() noexcept { stage = Stage::attack; level = 0.0f; }
    bool isActive() const noexcept { return stage != Stage::off; }
    void release (double releaseSeconds, double sr) noexcept
    {
        if (stage == Stage::off) return;
        stage = Stage::release;
        releaseStep = level / (float) juce::jmax (1.0, releaseSeconds * sr);
    }
    void kill() noexcept { stage = Stage::off; level = 0.0f; }

    // Advance one sample; returns the envelope level.
    float next (double attackSeconds, double decaySeconds, float sustainLevel, double sr) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                level += (float) (1.0 / juce::jmax (1.0, attackSeconds * sr));
                if (level >= 1.0f) { level = 1.0f; stage = Stage::decay; }
                break;
            case Stage::decay:
                level -= (float) ((1.0 - sustainLevel) / juce::jmax (1.0, decaySeconds * sr));
                if (level <= sustainLevel) { level = sustainLevel; stage = Stage::sustain; }
                break;
            case Stage::sustain: break;
            case Stage::release:
                level -= releaseStep;
                if (level <= 0.0f) { level = 0.0f; stage = Stage::off; }
                break;
            case Stage::off: break;
        }
        return level;
    }
};

// TPT state-variable low-pass (Zavalishin / Simper).
struct Svf
{
    float ic1eq = 0.0f, ic2eq = 0.0f;
    float g = 0.0f, a1 = 0.0f, a2 = 0.0f;

    void setCutoff (double cutoffHz, float resonance, double sr) noexcept
    {
        const double c = juce::jlimit (20.0, 0.45 * sr, cutoffHz);
        g = (float) std::tan (juce::MathConstants<double>::pi * c / sr);
        const float k = 2.0f - 1.9f * juce::jlimit (0.0f, 1.0f, resonance);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
    }
    float process (float in) noexcept
    {
        const float v1 = a1 * ic1eq + a2 * (in - ic2eq);
        const float v2 = ic2eq + g * v1;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return v2;
    }
    void reset() noexcept { ic1eq = ic2eq = 0.0f; }
};

inline float polyBlep (double t, double dt) noexcept
{
    if (t < dt)       { t /= dt;             return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt;  return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

enum class Wave { saw, square, triangle, sine };

inline float oscillator (Wave wave, double phase, double inc) noexcept
{
    switch (wave)
    {
        case Wave::saw:      return (float) (2.0 * phase - 1.0) - polyBlep (phase, inc);
        case Wave::square:
        {
            float v = phase < 0.5 ? 1.0f : -1.0f;
            v += polyBlep (phase, inc);
            double t2 = phase + 0.5; if (t2 >= 1.0) t2 -= 1.0;
            return v - polyBlep (t2, inc);
        }
        case Wave::triangle: return (float) (4.0 * std::abs (phase - 0.5) - 1.0);
        case Wave::sine:     return (float) std::sin (juce::MathConstants<double>::twoPi * phase);
    }
    return 0.0f;
}

inline double midiToHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

} // namespace beatmaker::engine::dsp
