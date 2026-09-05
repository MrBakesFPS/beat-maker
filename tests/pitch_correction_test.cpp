#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <dsp/PitchCorrection.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    juce::AudioBuffer<float> tone (double hz, float amp, int length, int channels = 2)
    {
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
            {
                // A few harmonics so the detector sees something voice-like
                const double t = i / sr;
                const double v = std::sin (juce::MathConstants<double>::twoPi * hz * t)
                               + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 2.0 * hz * t)
                               + 0.2 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * hz * t);
                b.setSample (ch, i, amp * (float) v / 1.6f);
            }
        return b;
    }

    double zeroCrossingHz (const juce::AudioBuffer<float>& buf, int from, int to)
    {
        // Fundamental via zero crossings of a low-passed copy (kills the harmonics' extra crossings)
        std::vector<float> lp ((size_t) to);
        float state = 0.0f;
        const float a = (float) std::exp (-juce::MathConstants<double>::twoPi * 300.0 / sr);
        for (int i = 0; i < to; ++i) { state = (1.0f - a) * buf.getSample (0, i) + a * state; lp[(size_t) i] = state; }
        int crossings = 0;
        for (int i = from + 1; i < to; ++i) if ((lp[(size_t) i - 1] < 0.0f) != (lp[(size_t) i] < 0.0f)) ++crossings;
        return crossings * 0.5 * sr / (to - from);
    }

    void run (Effect& fx, juce::AudioBuffer<float>& audio, const InsertParams& p, int block = 512)
    {
        for (int pos = 0; pos < audio.getNumSamples(); pos += block)
        {
            const int n = juce::jmin (block, audio.getNumSamples() - pos);
            juce::AudioBuffer<float> b (audio.getArrayOfWritePointers(), audio.getNumChannels(), pos, n);
            fx.process (b, n, p);
        }
    }

    double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
}

TEST_CASE ("Pitch detector finds the fundamental of tones and reports nothing for silence")
{
    PitchDetector det;
    det.prepare (sr);
    for (double hz : { 110.0, 220.0, 330.0, 440.0, 880.0 })
    {
        INFO (hz);
        det.reset();
        auto t = tone (hz, 0.5f, 9600, 1);
        det.push (t.getReadPointer (0), 9600);
        CHECK_THAT (det.detect(), WithinAbs (hz, hz * 0.01));
    }
    det.reset();
    std::vector<float> silence (9600, 0.0f);
    det.push (silence.data(), 9600);
    CHECK (det.detect() == 0.0f);
}

TEST_CASE ("Scale snapping picks the nearest note of the key")
{
    using PC = PitchCorrectionEffect;
    CHECK (PC::nearestScaleNote (69.4f, 0, PC::chromatic) == 69.0f);
    CHECK (PC::nearestScaleNote (69.6f, 0, PC::chromatic) == 70.0f);
    // C major has no Bb (70): 70.1 goes to B (71) since it's closer than A (69)... 70.1 -> A is 1.1 away, B 0.9 away
    CHECK (PC::nearestScaleNote (70.1f, 0, PC::major) == 71.0f);
    CHECK (PC::nearestScaleNote (69.9f, 0, PC::major) == 69.0f);
    // A minor contains C (72) but not C# (73)
    CHECK (PC::nearestScaleNote (73.2f, 9, PC::minor) == 74.0f);
    CHECK (PC::nearestScaleNote (72.8f, 9, PC::minor) == 72.0f);
    // Pentatonic: F# major pentatonic = F# G# A# C# D#; E (76) is closest to D# (75) or F# (78)? 76 -> 75
    CHECK (PC::nearestScaleNote (76.0f, 6, PC::majorPentatonic) == 75.0f);
    CHECK (juce::String (PC::keyName (9)) == "A");
    CHECK (juce::String (PC::scaleName (PC::minor)) == "Minor");
    REQUIRE (Effect::choices (EffectType::pitchCorrection, PC::key) != nullptr);
    CHECK (Effect::choices (EffectType::pitchCorrection, PC::key)->size() == 12);
    CHECK (Effect::choices (EffectType::pitchCorrection, PC::speed) == nullptr);
}

TEST_CASE ("Pitch correction pulls a flat note to pitch, respects amount, transposes, and reports latency")
{
    auto fx = Effect::create (EffectType::pitchCorrection, sr);
    REQUIRE (fx != nullptr);
    auto p = Effect::defaultParams (EffectType::pitchCorrection);
    p.values[PitchCorrectionEffect::speed] = 0.0f;
    p.values[PitchCorrectionEffect::formant] = 0.0f;
    const int latency = fx->getLatencySamples (p);
    CHECK (latency > 0);
    CHECK (latency < (int) sr);

    // 40 cents flat of A3
    const double flat = midiToHz (57.0 - 0.4);
    auto audio = tone (flat, 0.5f, 96000);
    run (*fx, audio, p);
    CHECK (audio.getMagnitude (0, 0, latency / 2) == 0.0f);                       // wet only: silent while the latency fills
    CHECK_THAT (zeroCrossingHz (audio, 48000, 96000), WithinAbs (220.0, 1.5));   // corrected to A3
    auto* pc = dynamic_cast<PitchCorrectionEffect*> (fx.get());
    REQUIRE (pc != nullptr);
    CHECK (pc->getDetectedMidi() > 56.0f);
    CHECK_THAT (pc->getTargetMidi(), WithinAbs (57.0, 0.01));

    // Amount 0 leaves it alone
    fx->reset();
    p.values[PitchCorrectionEffect::amount] = 0.0f;
    auto untouched = tone (flat, 0.5f, 96000);
    run (*fx, untouched, p);
    CHECK_THAT (zeroCrossingHz (untouched, 48000, 96000), WithinAbs (flat, 1.5));

    // Transpose +12 on top of full correction
    fx->reset();
    p.values[PitchCorrectionEffect::amount] = 100.0f;
    p.values[PitchCorrectionEffect::transpose] = 12.0f;
    auto up = tone (flat, 0.5f, 96000);
    run (*fx, up, p);
    CHECK_THAT (zeroCrossingHz (up, 48000, 96000), WithinAbs (440.0, 3.0));

    // Mix 0 is the dry signal, delayed by the latency so it stays aligned with the wet path
    fx->reset();
    p.values[PitchCorrectionEffect::transpose] = 0.0f;
    p.values[PitchCorrectionEffect::mix] = 0.0f;
    auto dry = tone (flat, 0.5f, 96000);
    auto reference = dry;
    run (*fx, dry, p);
    CHECK (dry.getSample (0, latency + 5000) == reference.getSample (0, 5000));
}

TEST_CASE ("Retune speed glides toward the target; scale snapping follows the key")
{
    auto fx = Effect::create (EffectType::pitchCorrection, sr);
    auto p = Effect::defaultParams (EffectType::pitchCorrection);
    p.values[PitchCorrectionEffect::formant] = 0.0f;
    p.values[PitchCorrectionEffect::speed] = 300.0f;
    const int latency = fx->getLatencySamples (p);

    const double flat = midiToHz (57.0 - 0.45);
    auto audio = tone (flat, 0.5f, 144000);   // 3 s
    run (*fx, audio, p);
    const double early = zeroCrossingHz (audio, latency + 2400, latency + 9600);   // ~50..200 ms in: still gliding
    const double late = zeroCrossingHz (audio, 96000, 144000);
    CHECK (early < 219.0);
    CHECK (early > flat - 4.0);   // zero-crossing resolution over this short window is ~3 Hz
    CHECK_THAT (late, WithinAbs (220.0, 1.5));

    // Key of C major: A#3 (58) is not in the scale, so a note near it snaps to A (57) or B (59)
    fx->reset();
    p.values[PitchCorrectionEffect::speed] = 0.0f;
    p.values[PitchCorrectionEffect::key] = 0.0f;
    p.values[PitchCorrectionEffect::scale] = (float) PitchCorrectionEffect::major;
    auto bb = tone (midiToHz (58.15), 0.5f, 96000);   // slightly above A#3
    run (*fx, bb, p);
    CHECK_THAT (zeroCrossingHz (bb, 48000, 96000), WithinAbs (midiToHz (59.0), 2.0));   // B3
}
