#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ClipEdits.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> makeDc (int length, float value)
    {
        juce::AudioBuffer<float> b (1, length);
        juce::FloatVectorOperations::fill (b.getWritePointer (0), value, length);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }
    std::shared_ptr<AutomationLane> gainLane (std::initializer_list<AutomationPoint> pts)
    {
        auto l = std::make_shared<AutomationLane>();
        l->param = ParamId::volume();
        l->points = pts;
        l->sortPoints();
        return l;
    }
}

TEST_CASE ("Clip gain line scales the clip sample-accurately, in source time, on both the ramp and per-sample paths")
{
    Transport t;
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip c; c.audio = makeDc (10000, 1.0f); c.length = 4000; c.sourceOffset = 1000; c.timelineStart = 500; c.strip = 0; c.gain = 0.5f;
    // Source-time lane: unity until source 2000, then linear down to 0 at source 4000, then hold
    c.gainLane = gainLane ({ { 1000, 1.0f }, { 2000, 1.0f }, { 4000, 0.0f } });
    snap->clips.push_back (c);
    snap->strips.push_back ({});
    graph.setSnapshot (std::move (snap));
    t.play();

    juce::AudioBuffer<float> out (2, 5000);
    for (int pos = 0; pos < 5000; pos += 333)   // odd block size: exercises both the ramp path and the breakpoint-inside path
    {
        const int n = juce::jmin (333, 5000 - pos);
        float* ptrs[2] = { out.getWritePointer (0, pos), out.getWritePointer (1, pos) };
        graph.renderBlock (ptrs, 2, n);
    }
    // timeline 500 -> source 1000 (gain 1.0); timeline 1500 -> source 2000 (1.0); timeline 2500 -> source 3000 (0.5); timeline 3500 -> source 4000 (0)
    CHECK (out.getSample (0, 499) == 0.0f);
    CHECK_THAT (out.getSample (0, 500),  WithinAbs (0.5f, 1e-4));
    CHECK_THAT (out.getSample (0, 1500), WithinAbs (0.5f, 1e-4));
    CHECK_THAT (out.getSample (0, 2500), WithinAbs (0.25f, 2e-3));
    CHECK_THAT (out.getSample (0, 3499), WithinAbs (0.0f, 2e-3));
    CHECK_THAT (out.getSample (0, 4000), WithinAbs (0.0f, 1e-6));
    CHECK (out.getSample (0, 4500) == 0.0f);   // past the clip
    graph.collectGarbage();
}

TEST_CASE ("Clip gain line commands are undoable and survive trims and splits because times are in source samples")
{
    Session s;
    Track t; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));
    AudioClip c; c.audio = makeDc (10000, 1.0f); c.sampleRate = 48000.0; c.timelineStart = 0; c.length = 8000;
    s.execute (std::make_unique<AddClipCommand> (0, c));
    const ClipRef ref { 0, ClipRef::Kind::audio, 0 };

    s.execute (std::make_unique<SetClipGainLaneCommand> (ref, gainLane ({ { 0, 1.0f }, { 4000, 0.25f } })));
    REQUIRE (s.getTracks()[0].clips[0].gainLane != nullptr);
    CHECK (s.getHistory().getUndoName() == "Clip Gain");
    CHECK (buildRenderSnapshot (s)->clips[0].gainLane == s.getTracks()[0].clips[0].gainLane);

    // Trim the start by 2000: the point at source 4000 is still at source 4000
    s.execute (std::make_unique<TrimClipCommand> (ref, 2000, 6000));
    CHECK (s.getTracks()[0].clips[0].gainLane->points[1].time == 4000);
    CHECK_THAT (s.getTracks()[0].clips[0].gainLane->valueAt (4000, 1.0f), WithinAbs (0.25f, 1e-6));

    // Split: both halves keep the line
    s.execute (std::make_unique<SplitClipCommand> (ref, 5000));
    REQUIRE (s.getTracks()[0].clips.size() == 2);
    CHECK (s.getTracks()[0].clips[1].gainLane == s.getTracks()[0].clips[0].gainLane);
    s.undo(); s.undo();

    // An empty lane removes it
    s.execute (std::make_unique<SetClipGainLaneCommand> (ref, std::make_shared<AutomationLane>()));
    CHECK (s.getTracks()[0].clips[0].gainLane == nullptr);
    s.undo();
    CHECK (s.getTracks()[0].clips[0].gainLane != nullptr);
    s.undo();
    CHECK (s.getTracks()[0].clips[0].gainLane == nullptr);
}

TEST_CASE ("Scrubbing slides toward the target, reads the strip's clips with interpolation, and moves the playhead")
{
    Transport t;
    t.setSampleRate (48000.0);
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    // A ramp so the value read tells us the position: sample i has value i / 100000
    juce::AudioBuffer<float> ramp (1, 100000);
    for (int i = 0; i < 100000; ++i) ramp.setSample (0, i, (float) i / 100000.0f);
    RenderClip c; c.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (ramp)); c.length = 100000; c.strip = 0;
    snap->clips.push_back (c);
    snap->strips.push_back ({});
    graph.setSnapshot (std::move (snap));

    t.setPositionSamples (10000);   // stopped: scrub starts from the playhead
    graph.startScrub (0, 30000);
    CHECK (graph.isScrubbing());

    juce::AudioBuffer<float> out (2, 512);
    float first = -1.0f, last = -1.0f;
    for (int b = 0; b < 40; ++b)     // ~0.43 s of scrubbing toward 30000
    {
        graph.renderBlock (out.getArrayOfWritePointers(), 2, 512);
        if (b == 0) first = out.getSample (0, 5);
        last = out.getSample (0, 511);
    }
    CHECK (first > 0.09f);  CHECK (first < 0.12f);          // started near sample 10000
    CHECK (last > 0.28f);   CHECK (last < 0.31f);           // settled near 30000
    CHECK (t.getPositionSamples() > 29000);
    CHECK (t.getPositionSamples() < 31000);
    CHECK_FALSE (t.isPlaying());                             // scrubbing never starts the transport

    // Parked at the target: silence
    for (int b = 0; b < 20; ++b) graph.renderBlock (out.getArrayOfWritePointers(), 2, 512);
    CHECK (out.getMagnitude (0, 0, 512) < 1.0e-3f);

    graph.stopScrub();
    CHECK_FALSE (graph.isScrubbing());
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 512);
    CHECK (out.getMagnitude (0, 0, 512) == 0.0f);
    graph.collectGarbage();
}

TEST_CASE ("Pencil edits replace the clip audio non-destructively and undo restores the original buffer")
{
    Session s;
    Track t; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));
    auto original = makeDc (1000, 0.5f);
    AudioClip c; c.audio = original; c.sampleRate = 48000.0; c.length = 1000; c.sourceFile = juce::File ("/tmp/whatever.wav");
    s.execute (std::make_unique<AddClipCommand> (0, c));
    const ClipRef ref { 0, ClipRef::Kind::audio, 0 };

    auto edited = std::make_shared<juce::AudioBuffer<float>> (*original);
    for (int i = 100; i < 110; ++i) edited->setSample (0, i, 0.0f);   // draw a flat line over a click
    s.execute (std::make_unique<ReplaceClipAudioCommand> (ref, edited));

    const auto& clip = s.getTracks()[0].clips[0];
    CHECK (clip.audio == edited);
    CHECK (clip.audioModified);
    CHECK (clip.sourceFile.getFileName() == "whatever.wav");     // file reference kept
    CHECK (original->getSample (0, 105) == 0.5f);                 // original untouched
    CHECK (buildRenderSnapshot (s)->clips[0].audio == edited);    // engine plays the edit

    s.undo();
    CHECK (s.getTracks()[0].clips[0].audio == original);
    CHECK_FALSE (s.getTracks()[0].clips[0].audioModified);
    s.redo();
    CHECK (s.getTracks()[0].clips[0].audio == edited);
}
