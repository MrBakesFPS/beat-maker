#include <catch2/catch_test_macros.hpp>
#include <Playlists.h>
#include <Session.h>

using namespace beatmaker::model;

namespace
{
    AudioClip clipAt (juce::int64 start, juce::int64 len, juce::int64 offset = 0, const char* name = "c")
    {
        AudioClip c; c.name = name; c.audio = std::make_shared<const juce::AudioBuffer<float>> (1, 100000);
        c.sampleRate = 48000.0; c.timelineStart = start; c.length = len; c.sourceOffset = offset;
        c.fadeIn = 100; c.fadeOut = 100;
        return c;
    }
}

TEST_CASE ("Playlist commands: new, duplicate, switch, delete, add take - all undoable")
{
    Session s;
    Track t; t.name = "Vox"; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));
    s.execute (std::make_unique<AddClipCommand> (0, clipAt (0, 1000)));
    auto& track = s.getTracks()[0];

    s.execute (std::make_unique<NewPlaylistCommand> (0));
    CHECK (track.clips.empty());
    REQUIRE (track.alternates.size() == 1);
    CHECK (track.alternates[0].name == "Vox.01");
    CHECK (track.alternates[0].clips.size() == 1);
    CHECK (track.mainPlaylistName == "Vox.02");
    s.undo();
    CHECK (track.clips.size() == 1);
    CHECK (track.alternates.empty());
    CHECK (track.mainPlaylistName.isEmpty());
    s.redo();

    s.execute (std::make_unique<AddClipCommand> (0, clipAt (5000, 500)));
    s.execute (std::make_unique<DuplicatePlaylistCommand> (0));
    REQUIRE (track.alternates.size() == 2);
    CHECK (track.alternates[1].name == "Vox.02");
    CHECK (track.alternates[1].clips.size() == 1);
    CHECK (track.clips.size() == 1);                 // main keeps editing the copy
    CHECK (track.mainPlaylistName == "Vox.03");

    s.execute (std::make_unique<SwitchPlaylistCommand> (0, 0));
    CHECK (track.mainPlaylistName == "Vox.01");
    CHECK (track.clips[0].timelineStart == 0);
    CHECK (track.alternates[0].name == "Vox.03");
    CHECK (track.alternates[0].clips[0].timelineStart == 5000);
    s.undo();
    CHECK (track.mainPlaylistName == "Vox.03");
    CHECK (track.clips[0].timelineStart == 5000);

    s.execute (std::make_unique<DeletePlaylistCommand> (0, 1));
    REQUIRE (track.alternates.size() == 1);
    s.undo();
    REQUIRE (track.alternates.size() == 2);
    CHECK (track.alternates[1].name == "Vox.02");

    Playlist take; take.name = "Vox.09"; take.clips.push_back (clipAt (0, 10));
    s.execute (std::make_unique<AddAlternatePlaylistCommand> (0, take));
    CHECK (track.alternates.size() == 3);
    s.undo();
    CHECK (track.alternates.size() == 2);

    // Out-of-range switch/delete are harmless
    s.execute (std::make_unique<SwitchPlaylistCommand> (0, 7));
    s.execute (std::make_unique<DeletePlaylistCommand> (0, 7));
    CHECK (track.alternates.size() == 2);
}

TEST_CASE ("Slice and clear ranges trim clips non-destructively and kill fades at cuts")
{
    std::vector<AudioClip> clips { clipAt (1000, 1000, 50, "a"), clipAt (3000, 500, 0, "b"), clipAt (5000, 1000, 0, "c") };

    auto slice = Playlists::sliceRange (clips, 1500, 5500);
    REQUIRE (slice.size() == 3);
    CHECK (slice[0].timelineStart == 1500); CHECK (slice[0].length == 500); CHECK (slice[0].sourceOffset == 550);
    CHECK (slice[0].fadeIn == 0);  CHECK (slice[0].fadeOut == 100);    // cut at the start only
    CHECK (slice[1].timelineStart == 3000); CHECK (slice[1].length == 500); CHECK (slice[1].fadeIn == 100);
    CHECK (slice[2].timelineStart == 5000); CHECK (slice[2].length == 500); CHECK (slice[2].fadeOut == 0);

    auto cleared = Playlists::clearRange (clips, 1500, 5500);
    REQUIRE (cleared.size() == 2);
    CHECK (cleared[0].name == "a"); CHECK (cleared[0].timelineStart == 1000); CHECK (cleared[0].length == 500); CHECK (cleared[0].fadeOut == 0);
    CHECK (cleared[1].name == "c"); CHECK (cleared[1].timelineStart == 5500); CHECK (cleared[1].length == 500); CHECK (cleared[1].sourceOffset == 500); CHECK (cleared[1].fadeIn == 0);

    // A range inside one clip splits it in two
    auto split = Playlists::clearRange ({ clipAt (0, 1000) }, 300, 600);
    REQUIRE (split.size() == 2);
    CHECK (split[0].length == 300);
    CHECK (split[1].timelineStart == 600); CHECK (split[1].sourceOffset == 600); CHECK (split[1].length == 400);
}

TEST_CASE ("Comp replaces the range in the main playlist with the alternate's audio")
{
    std::vector<AudioClip> main { clipAt (0, 4000, 0, "main") };
    std::vector<AudioClip> alt  { clipAt (0, 4000, 9000, "take2") };

    auto comped = Playlists::comp (main, alt, 1000, 2000);
    REQUIRE (comped.size() == 3);
    CHECK (comped[0].name == "main");  CHECK (comped[0].timelineStart == 0);    CHECK (comped[0].length == 1000);
    CHECK (comped[1].name == "take2"); CHECK (comped[1].timelineStart == 1000); CHECK (comped[1].length == 1000); CHECK (comped[1].sourceOffset == 10000);
    CHECK (comped[2].name == "main");  CHECK (comped[2].timelineStart == 2000); CHECK (comped[2].length == 2000); CHECK (comped[2].sourceOffset == 2000);

    // Through the session it is a single undoable step
    Session s;
    Track t; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));
    s.execute (std::make_unique<AddClipCommand> (0, main[0]));
    s.execute (std::make_unique<ReplaceMainClipsCommand> (0, comped, "Comp"));
    CHECK (s.getTracks()[0].clips.size() == 3);
    CHECK (s.getHistory().getUndoName() == "Comp");
    s.undo();
    CHECK (s.getTracks()[0].clips.size() == 1);
}

TEST_CASE ("Loop passes split a continuous recording at the cycle boundaries")
{
    // Loop 10000..30000 (length 20000); recording started at 15000 and ran 55000 samples
    auto passes = Playlists::loopPasses (15000, 55000, 10000, 30000);
    REQUIRE (passes.size() == 3);   // 15000 + 20000 + 20000 = 55000 exactly
    CHECK (passes[0].fileOffset == 0);     CHECK (passes[0].length == 15000); CHECK (passes[0].timelineStart == 15000);   // partial first pass
    CHECK (passes[1].fileOffset == 15000); CHECK (passes[1].length == 20000); CHECK (passes[1].timelineStart == 10000);
    CHECK (passes[2].fileOffset == 35000); CHECK (passes[2].length == 20000); CHECK (passes[2].timelineStart == 10000);

    // A partial final pass is its own take
    passes = Playlists::loopPasses (10000, 45000, 10000, 30000);
    REQUIRE (passes.size() == 3);
    CHECK (passes[2].fileOffset == 40000); CHECK (passes[2].length == 5000); CHECK (passes[2].timelineStart == 10000);

    // A short trailing pass below the minimum is dropped
    passes = Playlists::loopPasses (10000, 41000, 10000, 30000, 4800);
    REQUIRE (passes.size() == 2);
    CHECK (passes[1].fileOffset == 20000);
    CHECK (passes[1].length == 20000);

    // Not looping (or recording started outside the loop): one take
    passes = Playlists::loopPasses (500, 1000, 0, 0);
    REQUIRE (passes.size() == 1);
    CHECK (passes[0].timelineStart == 500);
    passes = Playlists::loopPasses (50000, 1000, 10000, 30000);
    REQUIRE (passes.size() == 1);
    CHECK (passes[0].length == 1000);
}
