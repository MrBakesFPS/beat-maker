#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <dsp/DrumMachine.h>
#include <dsp/Synth.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> makeDc (int length, float value)
    {
        juce::AudioBuffer<float> b (1, length);
        juce::FloatVectorOperations::fill (b.getWritePointer (0), value, length);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }
}

TEST_CASE ("Pan law: unity at centre, +3 dB hard side, silence on the far side, extra channels untouched")
{
    CHECK_THAT (panGainForChannel (0.0f, 0), WithinAbs (1.0f, 1e-6));
    CHECK_THAT (panGainForChannel (0.0f, 1), WithinAbs (1.0f, 1e-6));
    CHECK_THAT (panGainForChannel (-1.0f, 0), WithinAbs (juce::MathConstants<float>::sqrt2, 1e-6));
    CHECK_THAT (panGainForChannel (-1.0f, 1), WithinAbs (0.0f, 1e-6));
    CHECK_THAT (panGainForChannel (1.0f, 0), WithinAbs (0.0f, 1e-6));
    CHECK_THAT (panGainForChannel (1.0f, 1), WithinAbs (juce::MathConstants<float>::sqrt2, 1e-6));
    CHECK_THAT (panGainForChannel (0.7f, 2), WithinAbs (1.0f, 1e-6));
    // Equal power: L^2 + R^2 == 2 everywhere
    for (float pan : { -0.8f, -0.3f, 0.0f, 0.5f, 0.9f })
    {
        const float l = panGainForChannel (pan, 0), r = panGainForChannel (pan, 1);
        CHECK_THAT (l * l + r * r, WithinAbs (2.0f, 1e-5));
    }
}

TEST_CASE ("Audio clip pan applies per output channel")
{
    Transport t;
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back ({ makeDc (100, 0.5f), 0, 0, 100, 1.0f, /*pan*/ -1.0f });
    graph.setSnapshot (std::move (snap));
    t.play();

    juce::AudioBuffer<float> out (2, 32);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 32);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f * juce::MathConstants<float>::sqrt2, 1e-6));
    CHECK_THAT (out.getSample (1, 10), WithinAbs (0.0f, 1e-6));
    graph.collectGarbage();
}

TEST_CASE ("Drum voices and synths honour pan")
{
    auto kit = std::make_shared<DrumKit>();
    kit->pads[0] = { "DC", makeDc (100, 1.0f), 1.0f };

    DrumMachine dm;
    juce::AudioBuffer<float> out (2, 16);
    out.clear();
    dm.trigger (kit.get(), 0, 1.0f, 1.0f, 0, /*pan*/ 1.0f);
    dm.render (out.getArrayOfWritePointers(), 2, 16);
    CHECK_THAT (out.getSample (0, 5), WithinAbs (0.0f, 1e-6));
    CHECK_THAT (out.getSample (1, 5), WithinAbs (juce::MathConstants<float>::sqrt2, 1e-6));

    SynthParams p;
    p.wave = SynthParams::Wave::sine; p.secondOscillator = false; p.cutoffHz = 20000.0f;
    p.attackSeconds = 0.0f; p.sustainLevel = 1.0f; p.gain = 1.0f;
    Synth synth;
    synth.prepare (48000.0);
    synth.setParams (&p);
    synth.setPan (-1.0f);
    synth.noteOn (69, 1.0f, 0, -1);
    out.clear();
    synth.render (out.getArrayOfWritePointers(), 2, 16);
    CHECK (out.getMagnitude (0, 0, 16) > 0.01f);
    CHECK_THAT (out.getMagnitude (1, 0, 16), WithinAbs (0.0f, 1e-6));
}

TEST_CASE ("Track mix and kit commands are undoable and flow into the snapshot")
{
    using namespace beatmaker::model;
    Session s;
    Track t; t.type = Track::Type::instrument; t.instrumentKind = Track::InstrumentKind::drumMachine;
    auto kit = std::make_shared<DrumKit>();
    kit->pads[0] = { "Kick", makeDc (10, 1.0f), 1.0f };
    t.drumKit = kit;
    s.execute (std::make_unique<AddTrackCommand> (t));

    PatternClip clip;
    clip.pattern = std::make_shared<const StepPattern>();
    clip.sampleRate = 48000.0;
    clip.length = 48000;
    s.execute (std::make_unique<AddPatternClipCommand> (0, clip));

    s.execute (std::make_unique<SetTrackMixCommand> (0, 0.5f, -0.25f));
    CHECK (s.getTracks()[0].gain == 0.5f);
    CHECK (s.getTracks()[0].pan == -0.25f);
    CHECK (s.getHistory().getUndoName() == "Adjust Volume/Pan");

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->patterns.size() == 1);
    CHECK (snap->patterns[0].gain == 0.5f);
    CHECK (snap->patterns[0].pan == -0.25f);

    auto louder = std::make_shared<DrumKit> (*kit);
    louder->pads[0].gain = 1.5f;
    s.execute (std::make_unique<ReplaceDrumKitCommand> (0, louder));
    CHECK (s.getTracks()[0].drumKit->pads[0].gain == 1.5f);
    CHECK (kit->pads[0].gain == 1.0f);           // original untouched
    s.undo();
    CHECK (s.getTracks()[0].drumKit == kit);
    s.undo();
    CHECK (s.getTracks()[0].gain == 1.0f);
    CHECK (s.getTracks()[0].pan == 0.0f);

    // Out-of-range values are clamped
    s.execute (std::make_unique<SetTrackMixCommand> (0, 9.0f, 5.0f));
    CHECK (s.getTracks()[0].gain == 2.0f);
    CHECK (s.getTracks()[0].pan == 1.0f);
}
