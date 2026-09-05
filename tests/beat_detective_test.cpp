#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <BeatDetective.h>
#include <Session.h>

using namespace beatmaker::model;
using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;
    constexpr juce::int64 beat = 24000;   // 120 BPM

    // One 4-second audio clip of clicks, split into pieces at given timeline starts.
    struct Fixture
    {
        Session session;
        Fixture (const std::vector<juce::int64>& starts, juce::int64 length = 12000)
        {
            Track t; t.name = "Drums"; t.type = Track::Type::audio;
            session.execute (std::make_unique<AddTrackCommand> (t));
            auto audio = std::make_shared<juce::AudioBuffer<float>> (1, 4 * 48000);
            audio->clear();
            for (int i = 0; i < audio->getNumSamples(); i += 6000) audio->setSample (0, i, 0.9f);   // a click every 125 ms
            for (size_t i = 0; i < starts.size(); ++i)
            {
                AudioClip c; c.name = "Hit " + juce::String (i); c.audio = audio; c.sampleRate = sr;
                c.timelineStart = starts[i]; c.sourceOffset = (juce::int64) i * 12000; c.length = length;
                session.execute (std::make_unique<AddClipCommand> (0, c));
            }
        }
        const AudioClip& clip (int i) const { return session.getTracks()[0].clips[(size_t) i]; }
        std::vector<ClipRef> all() const
        {
            std::vector<ClipRef> refs;
            for (int i = 0; i < (int) session.getTracks()[0].clips.size(); ++i) refs.push_back ({ 0, ClipRef::Kind::audio, i });
            return refs;
        }
    };
}

TEST_CASE ("Nearest grid line honours swing")
{
    ConformSettings c; c.bpm = 120.0; c.gridBeats = 0.25;   // 6000-sample 16ths
    CHECK (BeatDetective::nearestGridSample (100, c, sr) == 0);
    CHECK (BeatDetective::nearestGridSample (5900, c, sr) == 6000);
    CHECK (BeatDetective::nearestGridSample (14000, c, sr) == 12000);
    c.swing = 1.0f;   // odd 16ths land at the triplet position: 6000 + 2000
    CHECK (BeatDetective::nearestGridSample (7900, c, sr) == 8000);
    CHECK (BeatDetective::nearestGridSample (12100, c, sr) == 12000);   // even lines don't move
    c.swing = 0.5f;
    CHECK (BeatDetective::nearestGridSample (7000, c, sr) == 7000);
}

TEST_CASE ("Clip Conform moves clip starts to the grid with strength and exclusion")
{
    // Hits 400 samples late, 300 early, exactly on, and way off
    Fixture f ({ 400, beat / 4 - 300, beat / 2, beat * 3 / 4 + 2000 });
    ConformSettings c; c.bpm = 120.0; c.gridBeats = 0.25;

    auto cmd = BeatDetective::conform (f.session, f.all(), c);
    REQUIRE (cmd != nullptr);
    f.session.execute (std::move (cmd));
    CHECK (f.clip (0).timelineStart == 0);
    CHECK (f.clip (1).timelineStart == 6000);
    CHECK (f.clip (2).timelineStart == 12000);
    CHECK (f.clip (3).timelineStart == 18000);
    CHECK (f.session.getHistory().getUndoName() == "Clip Conform");
    f.session.undo();
    CHECK (f.clip (0).timelineStart == 400);
    CHECK (f.clip (3).timelineStart == 20000);

    // Half strength: halfway there
    c.strength = 0.5f;
    f.session.execute (BeatDetective::conform (f.session, f.all(), c));
    CHECK (f.clip (0).timelineStart == 200);
    CHECK (f.clip (1).timelineStart == 5850);
    CHECK (f.clip (3).timelineStart == 19000);
    f.session.undo();

    // Exclude within 20% of half a step (600 samples): the 400 and 300 offsets stay, the 2000 moves
    c.strength = 1.0f; c.excludeWithin = 0.2f;
    f.session.execute (BeatDetective::conform (f.session, f.all(), c));
    CHECK (f.clip (0).timelineStart == 400);
    CHECK (f.clip (1).timelineStart == 5700);
    CHECK (f.clip (3).timelineStart == 18000);

    // Nothing to do returns no command
    CHECK (BeatDetective::conform (f.session, f.all(), c) == nullptr);
}

TEST_CASE ("Edit Smoothing fills gaps up to the available audio and crossfades the joins")
{
    // Clips of 12000 samples every 15000: 3000-sample gaps; the last is already touching the one before it
    Fixture f ({ 0, 15000, 30000, 42000 });
    SmoothingSettings sm; sm.fillGaps = true; sm.crossfade = false;
    auto cmd = BeatDetective::smooth (f.session, f.all(), sm);
    REQUIRE (cmd != nullptr);
    f.session.execute (std::move (cmd));
    CHECK (f.clip (0).length == 15000);
    CHECK (f.clip (1).length == 15000);
    CHECK (f.clip (2).length == 12000);   // already touching
    CHECK (f.clip (3).length == 12000);   // last clip untouched
    CHECK (f.clip (0).fadeOut == 0);
    f.session.undo();
    CHECK (f.clip (0).length == 12000);

    sm.crossfade = true; sm.crossfadeMs = 10.0;   // 480 samples
    f.session.execute (BeatDetective::smooth (f.session, f.all(), sm));
    CHECK (f.clip (0).length == 15480);
    CHECK (f.clip (0).fadeOut == 480);
    CHECK (f.clip (0).fadeOutShape == FadeShape::equalPower);
    CHECK (f.clip (1).fadeIn == 480);
    CHECK (f.clip (1).fadeInShape == FadeShape::equalPower);
    CHECK (f.clip (1).fadeOut == 480);
    CHECK (f.clip (2).length == 12480);
    CHECK (f.clip (3).fadeIn == 480);
    CHECK (f.clip (3).fadeOut == 0);
    CHECK (f.session.getHistory().getUndoName() == "Edit Smoothing (fill and crossfade)");
    f.session.undo();
    CHECK (f.clip (1).fadeIn == 0);

    // Overlapping clips (from a conform that pulled one earlier) are trimmed back to the join
    Fixture g ({ 0, 10000 });
    f.session.execute (BeatDetective::smooth (g.session, g.all(), SmoothingSettings { true, false, 0.0 }));
    g.session.execute (BeatDetective::smooth (g.session, g.all(), SmoothingSettings { true, false, 0.0 }));
    CHECK (g.clip (0).length == 10000);

    // Fill is limited by the audio available after the clip's offset
    Fixture h ({ 0, 4 * 48000 - 100 });   // second clip starts near the end of the 4 s source
    h.session.execute (BeatDetective::smooth (h.session, h.all(), SmoothingSettings { true, false, 0.0 }));
    CHECK (h.clip (0).length == 4 * 48000 - 100);
}

TEST_CASE ("Separate at transients over several clips keeps every reference valid")
{
    Fixture f ({ 0, 30000 }, 24000);
    auto cmd = BeatDetective::separate (f.session, f.all());
    REQUIRE (cmd != nullptr);
    f.session.execute (std::move (cmd));
    // Each 24000-sample clip holds clicks every 6000 samples: the one at its start doesn't split, the other three do
    CHECK (f.session.getTracks()[0].clips.size() == 8);
    f.session.undo();
    CHECK (f.session.getTracks()[0].clips.size() == 2);
}
