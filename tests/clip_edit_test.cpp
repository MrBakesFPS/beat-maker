#include <catch2/catch_test_macros.hpp>
#include <ClipEdits.h>
#include <RenderSnapshotBuilder.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::model;
using beatmaker::engine::StepPattern;

namespace
{
    void populate (Session& s)
    {
        Track a; a.name = "Audio"; a.type = Track::Type::audio;
        s.execute (std::make_unique<AddTrackCommand> (a));
        Track b; b.name = "Audio 2"; b.type = Track::Type::audio;
        s.execute (std::make_unique<AddTrackCommand> (b));
        Track d; d.name = "Drums"; d.type = Track::Type::instrument; d.instrumentKind = Track::InstrumentKind::drumMachine;
        d.drumKit = std::make_shared<const beatmaker::engine::DrumKit>();
        s.execute (std::make_unique<AddTrackCommand> (d));
    }

    AudioClip audioClip (juce::int64 start, juce::int64 length, juce::int64 sourceLength = 10000)
    {
        AudioClip c;
        c.name = "a";
        c.audio = std::make_shared<const juce::AudioBuffer<float>> (1, (int) sourceLength);
        c.sampleRate = 48000.0;
        c.timelineStart = start;
        c.length = length;
        return c;
    }

    PatternClip patternClip (juce::int64 start, juce::int64 length)
    {
        PatternClip c;
        c.name = "p";
        c.pattern = std::make_shared<const StepPattern>();
        c.sampleRate = 48000.0;
        c.timelineStart = start;
        c.length = length;
        return c;
    }

    ClipTiming t (const Session& s, const ClipRef& r) { return *ClipEdits::timing (s, r); }
}

TEST_CASE ("ClipRef queries find clips by position and know which track kinds accept them")
{
    Session s; populate (s);
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (1000, 500)));
    s.execute (std::make_unique<AddPatternClipCommand> (2, patternClip (0, 4000)));

    const ClipRef audio { 0, ClipRef::Kind::audio, 0 };
    CHECK (ClipEdits::clipAt (s, 0, 1200) == audio);
    CHECK_FALSE (ClipEdits::clipAt (s, 0, 999).has_value());
    CHECK_FALSE (ClipEdits::clipAt (s, 0, 1500).has_value());     // end is exclusive
    CHECK (ClipEdits::clipAt (s, 2, 3999)->kind == ClipRef::Kind::pattern);

    CHECK (ClipEdits::canPlaceOn (s, audio, 1));
    CHECK_FALSE (ClipEdits::canPlaceOn (s, audio, 2));
    CHECK (ClipEdits::allClips (s, 2).size() == 1);
    CHECK (t (s, audio).maxLength == 10000);
    CHECK (t (s, { 2, ClipRef::Kind::pattern, 0 }).maxLength == 0);   // looping clips are unbounded
}

TEST_CASE ("Move within a track and across tracks, with undo")
{
    Session s; populate (s);
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (1000, 500)));
    const ClipRef ref { 0, ClipRef::Kind::audio, 0 };

    s.execute (std::make_unique<MoveClipCommand> (ref, 0, 4000));
    CHECK (t (s, ref).start == 4000);
    s.undo();
    CHECK (t (s, ref).start == 1000);

    auto move = std::make_unique<MoveClipCommand> (ref, 1, 2500);
    auto* raw = move.get();
    s.execute (std::move (move));
    CHECK (s.getTracks()[0].clips.empty());
    REQUIRE (s.getTracks()[1].clips.size() == 1);
    CHECK (raw->getResultingRef() == ClipRef { 1, ClipRef::Kind::audio, 0 });
    CHECK (s.getTracks()[1].clips[0].timelineStart == 2500);

    s.undo();
    REQUIRE (s.getTracks()[0].clips.size() == 1);
    CHECK (s.getTracks()[1].clips.empty());
    CHECK (s.getTracks()[0].clips[0].timelineStart == 1000);

    // Negative starts clamp to zero
    s.execute (std::make_unique<MoveClipCommand> (ref, 0, -50));
    CHECK (t (s, ref).start == 0);
}

TEST_CASE ("Trim keeps audio anchored to the timeline and respects the source bounds")
{
    Session s; populate (s);
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (1000, 5000, 6000)));
    const ClipRef ref { 0, ClipRef::Kind::audio, 0 };

    // Trim the start later by 300: offset grows, content stays put.
    s.execute (std::make_unique<TrimClipCommand> (ref, 1300, 4700));
    CHECK (t (s, ref).start == 1300);
    CHECK (t (s, ref).length == 4700);
    CHECK (t (s, ref).offset == 300);

    // Extend the start earlier further than the file allows: stops at offset 0.
    s.execute (std::make_unique<TrimClipCommand> (ref, 0, 6000));
    CHECK (t (s, ref).offset == 0);
    CHECK (t (s, ref).start == 1000);
    CHECK (t (s, ref).length == 6000);

    // Can't extend the end past the source
    s.execute (std::make_unique<TrimClipCommand> (ref, 1000, 9999));
    CHECK (t (s, ref).length == 6000);

    s.undo(); s.undo(); s.undo();
    CHECK (t (s, ref).start == 1000);
    CHECK (t (s, ref).length == 5000);
    CHECK (t (s, ref).offset == 0);
}

TEST_CASE ("Split creates a phase-preserving second half; duplicate appends a copy")
{
    Session s; populate (s);
    s.execute (std::make_unique<AddPatternClipCommand> (2, patternClip (1000, 8000)));
    const ClipRef ref { 2, ClipRef::Kind::pattern, 0 };

    auto split = std::make_unique<SplitClipCommand> (ref, 3000);
    auto* raw = split.get();
    s.execute (std::move (split));
    REQUIRE (s.getTracks()[2].patternClips.size() == 2);
    CHECK (t (s, ref).length == 2000);
    const auto second = raw->getSecondHalf();
    CHECK (t (s, second).start == 3000);
    CHECK (t (s, second).length == 6000);
    CHECK (t (s, second).offset == 2000);          // loop phase continues seamlessly

    s.execute (std::make_unique<DuplicateClipCommand> (second));
    REQUIRE (s.getTracks()[2].patternClips.size() == 3);
    CHECK (s.getTracks()[2].patternClips[2].timelineStart == 9000);
    CHECK (s.getTracks()[2].patternClips[2].loopOffset == 2000);

    s.undo(); s.undo();
    REQUIRE (s.getTracks()[2].patternClips.size() == 1);
    CHECK (t (s, ref).length == 8000);

    // Splitting outside the clip does nothing (and undo is harmless)
    s.execute (std::make_unique<SplitClipCommand> (ref, 50));
    CHECK (s.getTracks()[2].patternClips.size() == 1);
    s.undo();
    CHECK (s.getTracks()[2].patternClips.size() == 1);
}

TEST_CASE ("Remove works for every clip kind and restores at the same index")
{
    Session s; populate (s);
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (0, 100)));
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (200, 100)));
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (400, 100)));

    s.execute (std::make_unique<RemoveAnyClipCommand> (ClipRef { 0, ClipRef::Kind::audio, 1 }));
    REQUIRE (s.getTracks()[0].clips.size() == 2);
    CHECK (s.getTracks()[0].clips[1].timelineStart == 400);
    s.undo();
    REQUIRE (s.getTracks()[0].clips.size() == 3);
    CHECK (s.getTracks()[0].clips[1].timelineStart == 200);
}

TEST_CASE ("Shuffle repack lays clips end to end from the earliest, and compounds undo as one")
{
    Session s; populate (s);
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (1000, 500)));
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (5000, 300)));
    s.execute (std::make_unique<AddClipCommand> (0, audioClip (2000, 200)));

    auto compound = std::make_unique<CompoundCommand> ("Move + Shuffle");
    compound->add (std::make_unique<MoveClipCommand> (ClipRef { 0, ClipRef::Kind::audio, 1 }, 0, 1200));  // 5000 -> 1200
    compound->add (std::make_unique<RepackTrackCommand> (0));
    s.execute (std::move (compound));

    const auto& clips = s.getTracks()[0].clips;
    // Order by start after the move: [0]@1000 (500), [1]@1200 (300), [2]@2000 (200) -> packed 1000, 1500, 1800
    CHECK (clips[0].timelineStart == 1000);
    CHECK (clips[1].timelineStart == 1500);
    CHECK (clips[2].timelineStart == 1800);
    CHECK (s.getHistory().getUndoName() == "Move + Shuffle");

    s.undo();
    CHECK (clips[0].timelineStart == 1000);
    CHECK (clips[1].timelineStart == 5000);
    CHECK (clips[2].timelineStart == 2000);
}

TEST_CASE ("Loop offset shifts pattern phase: a split second half fires the right steps")
{
    using namespace beatmaker::engine;
    Transport transport;
    transport.setSampleRate (48000.0);
    transport.setBpm (120.0);              // step = 6000 samples
    AudioGraph graph (transport);

    auto kit = std::make_shared<DrumKit>();
    juce::AudioBuffer<float> impulse (1, 4); impulse.clear(); impulse.setSample (0, 0, 1.0f);
    kit->pads[0] = { "Impulse", std::make_shared<const juce::AudioBuffer<float>> (std::move (impulse)), 1.0f };

    auto pattern = std::make_shared<StepPattern>();
    pattern->set (0, 3, 127);              // only step 3 (18000 into each bar)

    // A clip starting at 20000 whose loop offset is 20000: pattern time zero
    // sits at timeline 0, so step 3 of iteration 1 lands at 96000 + 18000.
    auto snap = std::make_unique<RenderSnapshot>();
    RenderPattern rp;
    rp.pattern = pattern; rp.kit = kit; rp.timelineStart = 20000; rp.length = 200000; rp.loopOffset = 20000;
    snap->patterns.push_back (rp);
    graph.setSnapshot (std::move (snap));
    transport.play();

    juce::AudioBuffer<float> out (1, 130000);
    out.clear();
    for (int pos = 0; pos < 130000; pos += 500)
    {
        float* ptr = out.getWritePointer (0, pos);
        graph.renderBlock (&ptr, 1, 500);
    }
    std::vector<int> hits;
    for (int i = 0; i < 130000; ++i) if (out.getSample (0, i) != 0.0f) hits.push_back (i);
    CHECK (hits == std::vector<int> { 114000 });   // 18000 (before the clip) is skipped, 96000+18000 fires
    graph.collectGarbage();
}
