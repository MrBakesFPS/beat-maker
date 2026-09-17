#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ClipEdits.h>
#include <RenderSnapshotBuilder.h>
#include <dsp/Fades.h>
#include <dsp/Instrument.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> makeDc (int length, float value = 1.0f)
    {
        juce::AudioBuffer<float> b (1, length);
        juce::FloatVectorOperations::fill (b.getWritePointer (0), value, length);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }
}

TEST_CASE ("Fade shapes start at silence, end at unity, and equal power is -3 dB at the midpoint")
{
    for (auto shape : { FadeShape::linear, FadeShape::equalPower, FadeShape::sCurve })
    {
        CHECK_THAT (fadeGain (shape, 0.0), WithinAbs (0.0f, 1e-6));
        CHECK_THAT (fadeGain (shape, 1.0), WithinAbs (1.0f, 1e-6));
        CHECK (fadeGain (shape, 0.25) < fadeGain (shape, 0.75));   // monotonic
    }
    CHECK_THAT (fadeGain (FadeShape::linear, 0.5), WithinAbs (0.5f, 1e-6));
    CHECK_THAT (fadeGain (FadeShape::equalPower, 0.5), WithinAbs (0.70710678f, 1e-6));
    CHECK_THAT (fadeGain (FadeShape::sCurve, 0.5), WithinAbs (0.5f, 1e-6));
    CHECK (fadeGain (FadeShape::sCurve, 0.1) < fadeGain (FadeShape::linear, 0.1));   // slow start
    // Out of range clamps
    CHECK_THAT (fadeGain (FadeShape::linear, -1.0), WithinAbs (0.0f, 1e-6));
    CHECK_THAT (fadeGain (FadeShape::linear, 2.0), WithinAbs (1.0f, 1e-6));
}

TEST_CASE ("Clip envelope combines fade in and fade out")
{
    // 100 samples, 20 in, 30 out
    CHECK_THAT (clipEnvelopeAt (0, 100, 20, FadeShape::linear, 30, FadeShape::linear),  WithinAbs (0.0f, 1e-6));
    CHECK_THAT (clipEnvelopeAt (10, 100, 20, FadeShape::linear, 30, FadeShape::linear), WithinAbs (0.5f, 1e-6));
    CHECK_THAT (clipEnvelopeAt (50, 100, 20, FadeShape::linear, 30, FadeShape::linear), WithinAbs (1.0f, 1e-6));
    CHECK_THAT (clipEnvelopeAt (85, 100, 20, FadeShape::linear, 30, FadeShape::linear), WithinAbs (0.5f, 1e-6));
    CHECK_THAT (clipEnvelopeAt (99, 100, 20, FadeShape::linear, 30, FadeShape::linear), WithinAbs (1.0f / 30.0f, 1e-6));
    CHECK_THAT (clipEnvelopeAt (50, 100, 0, FadeShape::linear, 0, FadeShape::linear),   WithinAbs (1.0f, 1e-6));
}

TEST_CASE ("Graph renders fades sample-accurately across block boundaries and leaves the middle untouched")
{
    Transport t;
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip rc;
    rc.audio = makeDc (1000, 0.8f);
    rc.timelineStart = 100; rc.length = 1000; rc.gain = 1.0f;
    rc.fadeIn = 200; rc.fadeInShape = FadeShape::linear;
    rc.fadeOut = 100; rc.fadeOutShape = FadeShape::equalPower;
    snap->clips.push_back (rc);
    graph.setSnapshot (std::move (snap));
    t.play();

    juce::AudioBuffer<float> out (1, 1200);
    for (int pos = 0; pos < 1200; pos += 64)
    {
        float* ptr = out.getWritePointer (0, pos);
        graph.renderBlock (&ptr, 1, juce::jmin (64, 1200 - pos));
    }

    CHECK (out.getSample (0, 99) == 0.0f);                                          // before the clip
    CHECK_THAT (out.getSample (0, 100), WithinAbs (0.0f, 1e-6));                    // fade-in start
    CHECK_THAT (out.getSample (0, 200), WithinAbs (0.8f * 0.5f, 1e-6));             // halfway through the fade-in (sample 100 of 200)
    CHECK_THAT (out.getSample (0, 300), WithinAbs (0.8f, 1e-6));                    // fade-in done
    CHECK_THAT (out.getSample (0, 700), WithinAbs (0.8f, 1e-6));                    // steady state
    CHECK_THAT (out.getSample (0, 1050), WithinAbs (0.8f * 0.70710678f, 1e-5));     // halfway through the fade-out
    CHECK_THAT (out.getSample (0, 1099), WithinAbs (0.8f * fadeGain (FadeShape::equalPower, 1.0 / 100.0), 1e-5));
    CHECK (out.getSample (0, 1100) == 0.0f);                                        // after the clip
    graph.collectGarbage();
}

TEST_CASE ("A MIDI clip's fades envelope the instrument's rendered sound and a fade-out cuts the tail")
{
    Transport transport; transport.setSampleRate (48000.0); transport.setBpm (120.0);
    AudioGraph graph (transport);
    auto inst = std::shared_ptr<Instrument> (Instrument::create (InstrumentType::subtractive, 48000.0));
    auto params = std::make_shared<InstrumentParams> (Instrument::defaultParams (InstrumentType::subtractive));
    params->values[SubtractiveParams::wave] = 3.0f; params->values[SubtractiveParams::osc2] = 0.0f; params->values[SubtractiveParams::cutoff] = 20000.0f;
    params->values[SubtractiveParams::attack] = 0.0f; params->values[SubtractiveParams::sustain] = 1.0f; params->values[SubtractiveParams::level] = 1.0f;
    params->values[SubtractiveParams::filterEnv] = 0.0f; params->values[SubtractiveParams::release] = 2.0f;   // a long tail
    auto seq = std::make_shared<MidiSequence>(); seq->lengthBeats = 8.0; seq->notes.push_back ({ 69, 127, 0.0, 8.0 });   // held past the clip
    auto snap = std::make_unique<RenderSnapshot>();
    snap->instruments.push_back ({ 1, inst, params, 0 });
    RenderMidiClip clip; clip.sequence = seq; clip.instrumentId = 1; clip.strip = 0; clip.timelineStart = 0; clip.length = 48000;
    clip.fadeIn = 12000; clip.fadeInShape = FadeShape::linear; clip.fadeOut = 12000; clip.fadeOutShape = FadeShape::linear;
    snap->midiClips.push_back (clip);
    graph.setSnapshot (std::move (snap));
    transport.play();
    juce::AudioBuffer<float> out (1, 72000);
    out.setSize (1, 72000, false, true, true);
    for (int pos = 0; pos < 72000; pos += 512) { float* ptr = out.getWritePointer (0, pos); graph.renderBlock (&ptr, 1, juce::jmin (512, 72000 - pos)); }
    const float early = out.getMagnitude (0, 500, 2000), mid = out.getMagnitude (0, 20000, 8000), late = out.getMagnitude (0, 45500, 2000);
    CHECK (mid > 0.5f);
    CHECK (early < mid * 0.35f);           // fading in
    CHECK (late < mid * 0.35f);            // fading out
    CHECK (early > 0.01f); CHECK (late > 0.01f);
    CHECK (out.getMagnitude (0, 48000, 24000) == 0.0f);   // the release tail is cut with the fade-out
    graph.collectGarbage();

    // Without a fade-out the tail rings on past the clip
    auto snap2 = std::make_unique<RenderSnapshot>();
    auto inst2 = std::shared_ptr<Instrument> (Instrument::create (InstrumentType::subtractive, 48000.0));
    snap2->instruments.push_back ({ 1, inst2, params, 0 });
    clip.fadeIn = 0; clip.fadeOut = 0;
    snap2->midiClips.push_back (clip);
    transport.stop(); transport.setPositionSamples (0);
    graph.setSnapshot (std::move (snap2));
    transport.play();
    out.clear();
    for (int pos = 0; pos < 72000; pos += 512) { float* ptr = out.getWritePointer (0, pos); graph.renderBlock (&ptr, 1, juce::jmin (512, 72000 - pos)); }
    CHECK (out.getMagnitude (0, 49000, 4000) > 0.05f);
    graph.collectGarbage();
}

TEST_CASE ("Fade and gain commands clamp, undo, and survive trims and splits")
{
    Session s;
    Track a; a.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (a));
    AudioClip c;
    c.audio = makeDc (10000); c.sampleRate = 48000.0; c.timelineStart = 0; c.length = 10000;
    s.execute (std::make_unique<AddClipCommand> (0, c));
    const ClipRef ref { 0, ClipRef::Kind::audio, 0 };
    auto clip = [&] () -> const AudioClip& { return s.getTracks()[0].clips[0]; };

    s.execute (std::make_unique<SetClipFadesCommand> (ref, 3000, FadeShape::sCurve, 2000, FadeShape::equalPower));
    CHECK (clip().fadeIn == 3000);
    CHECK (clip().fadeOut == 2000);
    CHECK (clip().fadeInShape == FadeShape::sCurve);
    CHECK (clip().fadeOutShape == FadeShape::equalPower);

    // Overlapping fades: the fade-out is clamped to what is left
    s.execute (std::make_unique<SetClipFadesCommand> (ref, 8000, FadeShape::linear, 5000, FadeShape::linear));
    CHECK (clip().fadeIn == 8000);
    CHECK (clip().fadeOut == 2000);
    s.undo();
    CHECK (clip().fadeIn == 3000);

    s.execute (std::make_unique<SetClipGainCommand> (ref, juce::Decibels::decibelsToGain (-6.0f)));
    CHECK_THAT (juce::Decibels::gainToDecibels (clip().gain), WithinAbs (-6.0f, 1e-4));
    s.execute (std::make_unique<SetClipGainCommand> (ref, 100.0f));   // clamped to +12 dB
    CHECK_THAT (clip().gain, WithinAbs (4.0f, 1e-6));
    s.undo();
    CHECK_THAT (juce::Decibels::gainToDecibels (clip().gain), WithinAbs (-6.0f, 1e-4));

    // Trimming the clip shorter than its fades clamps them (and undo restores them)
    s.execute (std::make_unique<TrimClipCommand> (ref, 0, 4000));
    CHECK (clip().fadeIn == 3000);
    CHECK (clip().fadeOut == 1000);
    s.undo();
    CHECK (clip().fadeOut == 2000);

    // Splitting: the cut is hard on both sides
    auto split = std::make_unique<SplitClipCommand> (ref, 5000);
    auto* raw = split.get();
    s.execute (std::move (split));
    CHECK (clip().fadeIn == 3000);
    CHECK (clip().fadeOut == 0);
    const auto& tail = s.getTracks()[0].clips[(size_t) raw->getSecondHalf().index];
    CHECK (tail.fadeIn == 0);
    CHECK (tail.fadeOut == 2000);
    CHECK_THAT (tail.gain, WithinAbs (clip().gain, 1e-6));   // gain travels with both halves
    s.undo();
    CHECK (clip().fadeOut == 2000);

    // Everything flows into the render snapshot
    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->clips.size() == 1);
    CHECK (snap->clips[0].fadeIn == 3000);
    CHECK (snap->clips[0].fadeOut == 2000);
    CHECK (snap->clips[0].fadeInShape == FadeShape::sCurve);
    CHECK_THAT (snap->clips[0].gain, WithinAbs (clip().gain, 1e-6));   // track gain 1.0
}
