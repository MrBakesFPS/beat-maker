#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
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
    for (float pan : { -0.8f, -0.3f, 0.0f, 0.5f, 0.9f })
    {
        const float l = panGainForChannel (pan, 0), r = panGainForChannel (pan, 1);
        CHECK_THAT (l * l + r * r, WithinAbs (2.0f, 1e-5));
    }
}

TEST_CASE ("Strip pan applies to every source on the strip")
{
    Transport t;
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip rc; rc.audio = makeDc (100, 0.5f); rc.length = 100; rc.strip = 0;
    snap->clips.push_back (rc);
    RenderStrip strip; strip.pan = -1.0f;
    snap->strips.push_back (strip);
    graph.setSnapshot (std::move (snap));
    t.play();

    juce::AudioBuffer<float> out (2, 32);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 32);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f * juce::MathConstants<float>::sqrt2, 1e-6));
    CHECK_THAT (out.getSample (1, 10), WithinAbs (0.0f, 1e-6));
    graph.collectGarbage();
}

TEST_CASE ("Track mix and kit commands are undoable and flow into the snapshot strip")
{
    using namespace beatmaker::model;
    Session s;
    Track t; t.type = Track::Type::instrument; t.instrumentKind = Track::InstrumentKind::drumMachine;
    auto kit = std::make_shared<DrumKit>();
    kit->pads[0] = { "Kick", makeDc (10, 1.0f), 1.0f };
    t.drumKit = kit;
    s.execute (std::make_unique<AddTrackCommand> (t));

    s.execute (std::make_unique<SetTrackMixCommand> (0, 0.5f, -0.25f));
    CHECK (s.getTracks()[0].gain == 0.5f);
    CHECK (s.getTracks()[0].pan == -0.25f);
    CHECK (s.getHistory().getUndoName() == "Adjust Volume/Pan");

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->strips.size() == 1);
    CHECK (snap->strips[0].gain == 0.5f);
    CHECK (snap->strips[0].pan == -0.25f);

    auto louder = std::make_shared<DrumKit> (*kit);
    louder->pads[0].gain = 1.5f;
    s.execute (std::make_unique<ReplaceDrumKitCommand> (0, louder));
    CHECK (s.getTracks()[0].drumKit->pads[0].gain == 1.5f);
    CHECK (kit->pads[0].gain == 1.0f);
    s.undo();
    CHECK (s.getTracks()[0].drumKit == kit);
    s.undo();
    CHECK (s.getTracks()[0].gain == 1.0f);

    s.execute (std::make_unique<SetTrackMixCommand> (0, 9.0f, 5.0f));
    CHECK (s.getTracks()[0].gain == 2.0f);
    CHECK (s.getTracks()[0].pan == 1.0f);

    // Master strip is addressed as track -1
    s.execute (std::make_unique<SetTrackMixCommand> (-1, 0.25f, 0.0f));
    CHECK (s.getMaster().gain == 0.25f);
    CHECK (buildRenderSnapshot (s)->master.gain == 0.25f);
    s.undo();
    CHECK (s.getMaster().gain == 1.0f);
}
