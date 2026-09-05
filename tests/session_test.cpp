#include <catch2/catch_test_macros.hpp>
#include <RenderSnapshotBuilder.h>
#include <Session.h>

using namespace beatmaker::model;

namespace
{
    AudioClip clipOfLength (juce::int64 len, double start = 0.0)
    {
        AudioClip c;
        c.name = "c";
        c.audio = std::make_shared<const juce::AudioBuffer<float>> (1, (int) len);
        c.sampleRate = 44100.0;
        c.timelineStart = (juce::int64) (start * 44100.0);
        c.length = len;
        return c;
    }

    struct CountingListener : Session::Listener
    {
        int count = 0;
        void sessionChanged (Session&) override { ++count; }
    };
}

TEST_CASE ("Add track / add clip are undoable and notify listeners")
{
    Session s;
    CountingListener listener;
    s.addListener (&listener);

    Track t; t.name = "Drums";
    s.execute (std::make_unique<AddTrackCommand> (t));
    REQUIRE (s.getNumTracks() == 1);
    CHECK (listener.count == 1);

    s.execute (std::make_unique<AddClipCommand> (0, clipOfLength (44100, 1.0)));
    REQUIRE (s.getTracks()[0].clips.size() == 1);
    CHECK (s.getLengthSeconds() == 2.0);
    CHECK (s.getHistory().getUndoName() == "Add Clip");

    CHECK (s.undo());
    CHECK (s.getTracks()[0].clips.empty());
    CHECK (s.getLengthSeconds() == 0.0);

    CHECK (s.undo());
    CHECK (s.getNumTracks() == 0);
    CHECK_FALSE (s.undo());

    CHECK (s.redo());
    CHECK (s.redo());
    CHECK (s.getTracks()[0].clips.size() == 1);
    CHECK (listener.count == 6);

    s.removeListener (&listener);
}

TEST_CASE ("Snapshot builder honours mute, solo and gain")
{
    Session s;
    for (int i = 0; i < 3; ++i)
    {
        Track t; t.name = juce::String (i); t.gain = 0.5f;
        s.execute (std::make_unique<AddTrackCommand> (t));
        auto c = clipOfLength (10); c.gain = 2.0f;
        s.execute (std::make_unique<AddClipCommand> (i, c));
    }

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->clips.size() == 3);
    CHECK (snap->clips[0].gain == 1.0f); // 0.5 * 2.0

    s.execute (std::make_unique<SetTrackFlagCommand> (1, SetTrackFlagCommand::Flag::mute, true));
    CHECK (buildRenderSnapshot (s)->clips.size() == 2);

    s.execute (std::make_unique<SetTrackFlagCommand> (2, SetTrackFlagCommand::Flag::solo, true));
    CHECK (buildRenderSnapshot (s)->clips.size() == 1); // only the soloed track

    s.undo(); s.undo();
    CHECK (buildRenderSnapshot (s)->clips.size() == 3);
}
