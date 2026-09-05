#include "Synth.h"
#include <cmath>

namespace beatmaker::engine
{

void Synth::prepare (double sr) noexcept
{
    if (sr > 0.0) sampleRate = sr;
    reset();
}

void Synth::reset() noexcept
{
    for (auto& v : voices) v = {};
}

int Synth::getNumActiveVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

//==============================================================================
// Oscillators

float Synth::polyBlep (double t, double dt) noexcept
{
    if (t < dt)            { t /= dt;        return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt)      { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

float Synth::oscillator (SynthParams::Wave wave, double phase, double inc) noexcept
{
    switch (wave)
    {
        case SynthParams::Wave::saw:
            return (float) (2.0 * phase - 1.0) - polyBlep (phase, inc);

        case SynthParams::Wave::square:
        {
            float v = phase < 0.5 ? 1.0f : -1.0f;
            v += polyBlep (phase, inc);
            double t2 = phase + 0.5; if (t2 >= 1.0) t2 -= 1.0;
            v -= polyBlep (t2, inc);
            return v;
        }

        case SynthParams::Wave::triangle:
            return (float) (4.0 * std::abs (phase - 0.5) - 1.0);

        case SynthParams::Wave::sine:
            return (float) std::sin (juce::MathConstants<double>::twoPi * phase);
    }
    return 0.0f;
}

//==============================================================================
// Voices

void Synth::startVoice (Voice& v, int pitch, float velocity, int delay, int gate) noexcept
{
    const double detune = params != nullptr ? params->detuneCents : 0.0;
    const double freq = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);

    v = {};
    v.active   = true;
    v.pitch    = pitch;
    v.velocity = juce::jlimit (0.0f, 1.0f, velocity);
    v.delay    = juce::jmax (0, delay);
    v.gate     = gate;
    v.stage    = Stage::attack;
    v.inc1     = freq * std::pow (2.0, -detune / 1200.0) / sampleRate;
    v.inc2     = freq * std::pow (2.0,  detune / 1200.0) / sampleRate;
    v.phase2   = 0.37;   // offset so unison doesn't start perfectly in phase
    v.order    = ++orderCounter;
}

void Synth::noteOn (int pitch, float velocity, int delaySamples, int gateSamples) noexcept
{
    if (params == nullptr || ! juce::isPositiveAndBelow (pitch, 128))
        return;

    // Prefer a free voice, then a releasing one, then the oldest.
    Voice* target = nullptr;
    for (auto& v : voices) if (! v.active) { target = &v; break; }
    if (target == nullptr)
        for (auto& v : voices) if (v.stage == Stage::release && (target == nullptr || v.order < target->order)) target = &v;
    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices) if (v.order < target->order) target = &v;
    }

    startVoice (*target, pitch, velocity, delaySamples, gateSamples);
}

void Synth::noteOff (int pitch) noexcept
{
    for (auto& v : voices)
        if (v.active && v.pitch == pitch && v.gate < 0 && v.stage != Stage::release)
            v.gate = 0;   // release on the next sample
}

void Synth::allNotesOff (bool immediate) noexcept
{
    for (auto& v : voices)
    {
        if (! v.active) continue;
        if (immediate) { v.active = false; continue; }
        if (v.stage != Stage::release) v.gate = 0;
    }
}

void Synth::advanceEnvelope (Voice& v) noexcept
{
    const auto& p = *params;
    switch (v.stage)
    {
        case Stage::attack:
            v.env += (float) (1.0 / juce::jmax (1.0, p.attackSeconds * sampleRate));
            if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::decay; }
            break;
        case Stage::decay:
            v.env -= (float) ((1.0 - p.sustainLevel) / juce::jmax (1.0, p.decaySeconds * sampleRate));
            if (v.env <= p.sustainLevel) { v.env = p.sustainLevel; v.stage = Stage::sustain; }
            break;
        case Stage::sustain:
            break;
        case Stage::release:
            v.env -= v.releaseStep;
            if (v.env <= 0.0f) { v.env = 0.0f; v.stage = Stage::off; v.active = false; }
            break;
        case Stage::off:
            v.active = false;
            break;
    }
}

void Synth::render (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    if (params == nullptr) return;
    const auto& p = *params;
    const double nyquist = 0.45 * sampleRate;

    for (auto& v : voices)
    {
        if (! v.active) continue;

        const int offset = juce::jmin (v.delay, numSamples);
        v.delay -= offset;
        if (offset >= numSamples) continue;

        // Filter coefficients (updated every few samples as the envelope moves)
        float g = 0.0f, a1 = 0.0f, a2 = 0.0f;
        auto updateCoefficients = [&]
        {
            const double cutoff = juce::jlimit (20.0, nyquist, (double) p.cutoffHz * std::pow (2.0, p.filterEnvOctaves * v.env));
            g = (float) std::tan (juce::MathConstants<double>::pi * cutoff / sampleRate);
            const float k = 2.0f - 1.9f * juce::jlimit (0.0f, 1.0f, p.resonance);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
        };

        for (int i = offset; i < numSamples && v.active; ++i)
        {
            if (((i - offset) % coefficientInterval) == 0)
                updateCoefficients();

            // Gate -> release
            if (v.gate == 0 && v.stage != Stage::release)
            {
                v.stage = Stage::release;
                v.releaseStep = v.env / (float) juce::jmax (1.0, p.releaseSeconds * sampleRate);
                v.gate = -1;
            }
            else if (v.gate > 0)
                --v.gate;

            advanceEnvelope (v);
            if (! v.active) break;

            // Oscillators
            float osc = oscillator (p.wave, v.phase1, v.inc1);
            v.phase1 += v.inc1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
            if (p.secondOscillator)
            {
                osc = 0.5f * (osc + oscillator (p.wave, v.phase2, v.inc2));
                v.phase2 += v.inc2; if (v.phase2 >= 1.0) v.phase2 -= 1.0;
            }

            // TPT state-variable low-pass
            const float v1 = a1 * v.ic1eq + a2 * (osc - v.ic2eq);
            const float v2 = v.ic2eq + g * v1;
            v.ic1eq = 2.0f * v1 - v.ic1eq;
            v.ic2eq = 2.0f * v2 - v.ic2eq;

            const float sample = v2 * v.env * v.velocity * p.gain;
            for (int ch = 0; ch < numOutputs; ++ch)
                if (outputs[ch] != nullptr) outputs[ch][i] += sample;
        }
    }
}

} // namespace beatmaker::engine
