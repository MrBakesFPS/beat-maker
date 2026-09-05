#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <dsp/Effects.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    juce::AudioBuffer<float> sineBuffer (double freq, float amp, int length = 48000, int channels = 2)
    {
        juce::AudioBuffer<float> b (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                b.setSample (ch, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sr));
        return b;
    }
    float rmsOf (const juce::AudioBuffer<float>& b, int ch, int from, int num) { return b.getRMSLevel (ch, from, num); }
    float dbOf (float v) { return juce::Decibels::gainToDecibels (v, -120.0f); }
    std::unique_ptr<Effect> make (EffectType t) { return Effect::create (t, sr); }
}

TEST_CASE ("Every effect type in the suite instantiates with defaults, has metadata, and is transparent-ish or bounded")
{
    for (auto type : Effect::availableTypes())
    {
        auto fx = make (type);
        REQUIRE (fx != nullptr);
        CHECK (fx->getType() == type);
        CHECK (juce::String (Effect::typeName (type)).isNotEmpty());
        const auto& info = Effect::paramInfo (type);
        CHECK (! info.empty());
        CHECK (info.size() <= InsertParams().values.size());
        auto p = Effect::defaultParams (type);
        for (size_t i = 0; i < info.size(); ++i) CHECK (p.values[i] == info[i].def);

        auto b = sineBuffer (440.0, 0.25f, 8192);
        fx->process (b, 8192, p);
        CHECK (b.getMagnitude (0, 0, 8192) < 4.0f);   // nothing blows up
        for (int i = 0; i < 8192; i += 97) CHECK (std::isfinite (b.getSample (0, i)));
    }
}

TEST_CASE ("7-band EQ: flat is identity, HPF cuts lows, LPF cuts highs, peaks boost their band, response maths agrees")
{
    auto eq = make (EffectType::eq);
    auto p = Effect::defaultParams (EffectType::eq);
    auto b = sineBuffer (1000.0, 0.5f, 4096);
    auto copy = b;
    eq->process (b, 4096, p);
    for (int i = 0; i < 4096; ++i) CHECK (b.getSample (0, i) == copy.getSample (0, i));

    // HPF at 500 Hz kills 50 Hz but leaves 5 kHz
    p.values[EqEffect::hpFreq] = 500.0f;
    eq->reset();
    auto lo = sineBuffer (50.0, 0.5f); eq->process (lo, 48000, p);
    CHECK (dbOf (rmsOf (lo, 0, 24000, 24000)) < dbOf (0.5f / juce::MathConstants<float>::sqrt2) - 30.0f);
    eq->reset();
    auto hi = sineBuffer (5000.0, 0.5f); eq->process (hi, 48000, p);
    CHECK_THAT (dbOf (rmsOf (hi, 0, 24000, 24000)), WithinAbs (dbOf (0.5f / juce::MathConstants<float>::sqrt2), 0.5f));

    // LPF at 2 kHz kills 15 kHz
    p = Effect::defaultParams (EffectType::eq);
    p.values[EqEffect::lpFreq] = 2000.0f;
    eq->reset();
    auto vh = sineBuffer (15000.0, 0.5f); eq->process (vh, 48000, p);
    CHECK (dbOf (rmsOf (vh, 0, 24000, 24000)) < dbOf (0.5f / juce::MathConstants<float>::sqrt2) - 25.0f);

    // MF +12 dB at 1 kHz boosts a 1 kHz sine by ~12 dB; the analytic response says the same
    p = Effect::defaultParams (EffectType::eq);
    p.values[EqEffect::midGain] = 12.0f; p.values[EqEffect::midFreq] = 1000.0f; p.values[EqEffect::midQ] = 2.0f;
    eq->reset();
    auto mid = sineBuffer (1000.0, 0.1f); eq->process (mid, 48000, p);
    CHECK_THAT (dbOf (rmsOf (mid, 0, 24000, 24000)) - dbOf (0.1f / juce::MathConstants<float>::sqrt2), WithinAbs (12.0f, 0.3f));
    const auto bands = EqEffect::coefficientsFor (p, sr);
    CHECK_THAT (Biquad::magnitudeDb (bands[EqEffect::numBands - 4], sr, 1000.0), WithinAbs (12.0, 0.05));   // the MF band
    CHECK (EqEffect::activeBands (p)[3]);
    CHECK_FALSE (EqEffect::activeBands (p)[0]);
}

TEST_CASE ("Limiter holds peaks at the ceiling and reports lookahead latency")
{
    auto lim = make (EffectType::limiter);
    auto p = Effect::defaultParams (EffectType::limiter);
    p.values[LimiterEffect::ceiling] = -6.0f;
    CHECK (lim->getLatencySamples (p) == 96);   // 2 ms at 48 kHz
    auto b = sineBuffer (100.0, 1.0f);           // 0 dBFS in
    lim->process (b, 48000, p);
    const float ceilingGain = juce::Decibels::decibelsToGain (-6.0f);
    CHECK (b.getMagnitude (0, 1000, 47000) <= ceilingGain + 1e-4f);
    CHECK (b.getMagnitude (0, 1000, 47000) > ceilingGain * 0.9f);   // it is limiting, not just attenuating
    CHECK (lim->getMeter() > 5.0f);
}

TEST_CASE ("Gate silences below threshold and passes above; de-esser reduces only the high band")
{
    auto gate = make (EffectType::gate);
    auto p = Effect::defaultParams (EffectType::gate);
    p.values[GateEffect::threshold] = -30.0f; p.values[GateEffect::range] = -90.0f; p.values[GateEffect::hold] = 0.0f;
    auto quiet = sineBuffer (200.0, 0.001f);   // -60 dBFS
    gate->process (quiet, 48000, p);
    CHECK (dbOf (rmsOf (quiet, 0, 24000, 24000)) < -80.0f);
    gate->reset();
    auto loud = sineBuffer (200.0, 0.5f);
    gate->process (loud, 48000, p);
    CHECK_THAT (rmsOf (loud, 0, 24000, 24000), WithinAbs (0.5f / juce::MathConstants<float>::sqrt2, 0.02f));

    auto de = make (EffectType::deesser);
    auto d = Effect::defaultParams (EffectType::deesser);
    d.values[DeEsserEffect::frequency] = 4000.0f; d.values[DeEsserEffect::threshold] = -40.0f; d.values[DeEsserEffect::range] = 12.0f;
    auto ess = sineBuffer (8000.0, 0.5f);
    de->process (ess, 48000, d);
    CHECK (dbOf (rmsOf (ess, 0, 24000, 24000)) < dbOf (0.5f / juce::MathConstants<float>::sqrt2) - 9.0f);   // ~12 dB shelf, an octave above the corner
    de->reset();
    auto low = sineBuffer (200.0, 0.5f);
    de->process (low, 48000, d);
    CHECK_THAT (dbOf (rmsOf (low, 0, 24000, 24000)), WithinAbs (dbOf (0.5f / juce::MathConstants<float>::sqrt2), 0.5f));
}

TEST_CASE ("Chorus, flanger and phaser modulate the signal; mix 0 is identity")
{
    for (auto type : { EffectType::chorus, EffectType::flanger, EffectType::phaser })
    {
        auto fx = make (type);
        auto p = Effect::defaultParams (type);
        auto mixIndex = type == EffectType::phaser ? (size_t) PhaserEffect::mix : (size_t) ModulatedDelayEffect::mix;

        p.values[mixIndex] = 0.0f;
        auto dry = sineBuffer (440.0, 0.3f, 8192);
        auto copy = dry;
        fx->process (dry, 8192, p);
        for (int i = 0; i < 8192; i += 13) CHECK_THAT (dry.getSample (0, i), WithinAbs (copy.getSample (0, i), 1e-5f));

        p.values[mixIndex] = 100.0f;
        fx->reset();
        auto wet = sineBuffer (440.0, 0.3f, 48000);
        fx->process (wet, 48000, p);
        // The modulated output differs from the input over time but stays bounded
        float maxDiff = 0.0f;
        for (int i = 4800; i < 48000; i += 7) maxDiff = juce::jmax (maxDiff, std::abs (wet.getSample (0, i) - copy.getSample (0, i % 8192)));
        CHECK (maxDiff > 0.05f);
        CHECK (wet.getMagnitude (0, 0, 48000) < 1.0f);
    }
}

TEST_CASE ("Saturation soft-clips, hard-clips, and is near-linear at low drive; amp sim is bounded; utility handles gain/phase/width/mono")
{
    auto sat = make (EffectType::saturation);
    auto p = Effect::defaultParams (EffectType::saturation);
    p.values[SaturationEffect::drive] = 30.0f; p.values[SaturationEffect::output] = 0.0f; p.values[SaturationEffect::tone] = 20000.0f;
    auto b = sineBuffer (100.0, 0.5f, 4800);
    sat->process (b, 4800, p);
    CHECK (b.getMagnitude (0, 0, 4800) <= 1.0f);
    CHECK (b.getMagnitude (0, 0, 4800) > 0.95f);      // driven into the soft ceiling (tanh -> 1.0 in float at this drive)
    p.values[SaturationEffect::type] = 1.0f;
    sat->reset();
    b = sineBuffer (100.0, 0.5f, 4800);
    sat->process (b, 4800, p);
    CHECK_THAT (b.getMagnitude (0, 0, 4800), WithinAbs (1.0f, 1e-5f));   // hard clip exactly at 1
    p.values[SaturationEffect::type] = 0.0f; p.values[SaturationEffect::drive] = 0.0f;
    sat->reset();
    b = sineBuffer (100.0, 0.1f, 4800);
    sat->process (b, 4800, p);
    CHECK_THAT (b.getSample (0, 120), WithinAbs (std::tanh (0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 100.0 * 120 / sr)), 2e-3f));

    auto amp = make (EffectType::ampSim);
    auto a = Effect::defaultParams (EffectType::ampSim);
    a.values[AmpSimEffect::gain] = 40.0f;
    auto gtr = sineBuffer (220.0, 0.5f, 9600);
    amp->process (gtr, 9600, a);
    CHECK (gtr.getMagnitude (0, 4800, 4800) < 1.0f);
    CHECK (gtr.getMagnitude (0, 4800, 4800) > 0.05f);

    auto util = make (EffectType::utility);
    auto u = Effect::defaultParams (EffectType::utility);
    u.values[UtilityEffect::gain] = -6.0206f; u.values[UtilityEffect::invertR] = 1.0f;
    juce::AudioBuffer<float> st (2, 4); st.clear();
    st.setSample (0, 0, 0.8f); st.setSample (1, 0, 0.8f);
    util->process (st, 4, u);
    CHECK_THAT (st.getSample (0, 0), WithinAbs (0.4f, 1e-4f));
    CHECK_THAT (st.getSample (1, 0), WithinAbs (-0.4f, 1e-4f));
    u = Effect::defaultParams (EffectType::utility);
    u.values[UtilityEffect::mono] = 1.0f;
    st.clear(); st.setSample (0, 0, 1.0f); st.setSample (1, 0, 0.0f);
    util->process (st, 4, u);
    CHECK_THAT (st.getSample (0, 0), WithinAbs (0.5f, 1e-6f));
    CHECK_THAT (st.getSample (1, 0), WithinAbs (0.5f, 1e-6f));
    u = Effect::defaultParams (EffectType::utility);
    u.values[UtilityEffect::width] = 200.0f;
    st.clear(); st.setSample (0, 0, 1.0f); st.setSample (1, 0, 0.0f);
    util->process (st, 4, u);
    CHECK_THAT (st.getSample (0, 0), WithinAbs (1.5f, 1e-6f));    // mid 0.5 + side 0.5*2
    CHECK_THAT (st.getSample (1, 0), WithinAbs (-0.5f, 1e-6f));
}

TEST_CASE ("Delay modes: ping-pong bounces a mono input between channels; tape mode stays bounded with high feedback")
{
    auto delay = make (EffectType::delay);
    auto p = Effect::defaultParams (EffectType::delay);
    p.values[DelayEffect::time] = 100.0f; p.values[DelayEffect::feedback] = 50.0f; p.values[DelayEffect::mix] = 100.0f; p.values[DelayEffect::mode] = 2.0f;
    juce::AudioBuffer<float> b (2, 20000); b.clear();
    b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);   // one click in both channels
    delay->process (b, 20000, p);
    // First repeat (4800): only the left line was fed (mono sum) -> left has it, right is silent
    CHECK (std::abs (b.getSample (0, 4800)) > 0.4f);
    CHECK (std::abs (b.getSample (1, 4800)) < 1e-4f);
    // Second repeat (9600): the left line fed the right one
    CHECK (std::abs (b.getSample (1, 9600)) > 0.1f);
    CHECK (std::abs (b.getSample (0, 9600)) < 1e-4f);

    p.values[DelayEffect::mode] = 1.0f; p.values[DelayEffect::feedback] = 95.0f;
    delay->reset();
    auto tone = sineBuffer (1000.0, 0.9f, 96000);
    delay->process (tone, 96000, p);
    CHECK (tone.getMagnitude (0, 48000, 48000) < 3.0f);   // tape saturation keeps the loop from running away
    for (int i = 0; i < 96000; i += 101) CHECK (std::isfinite (tone.getSample (0, i)));
}
