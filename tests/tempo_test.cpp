#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <Session.h>
#include <ClipEdits.h>

using namespace beatmaker;
using Catch::Matchers::WithinAbs;

TEST_CASE ("SetTempoCommand scales clips, markers and automation to keep bar positions, and undoes")
{
    model::Session session;
    const double rate = 48000.0;
    {
        model::Track t; t.name = "Audio"; t.type = model::Track::Type::audio;
        model::AudioClip a; a.audio = std::make_shared<juce::AudioBuffer<float>> (1, 4800); a.sampleRate = rate; a.timelineStart = 96000; a.length = 4800;
        t.clips.push_back (a);
        model::Playlist alt; alt.name = "take 2"; model::AudioClip b = a; b.timelineStart = 48000; alt.clips.push_back (b); t.alternates.push_back (alt);
        auto lane = std::make_shared<engine::AutomationLane>(); lane->param = { engine::ParamId::Type::volume, 0, 0 };
        lane->points = { { 0, 0.5f }, { 96000, 1.0f } };
        t.automation.push_back (lane);
        session.execute (std::make_unique<model::AddTrackCommand> (t));
    }
    {
        model::Track t; t.name = "Keys"; t.type = model::Track::Type::instrument;
        model::MidiClip m; m.sampleRate = rate; m.timelineStart = 192000; m.length = 96000; m.loopOffset = 4800;
        t.midiClips.push_back (m);
        session.execute (std::make_unique<model::AddTrackCommand> (t));
    }
    session.execute (std::make_unique<model::AddMarkerCommand> (model::Marker { 0, "Chorus", 4.0, 6.0 }));
    REQUIRE (session.getBpm() == 120.0);

    // 120 -> 60 BPM: everything takes twice as long in seconds
    session.execute (std::make_unique<model::SetTempoCommand> (60.0, 3, true));
    CHECK (session.getBpm() == 60.0);
    CHECK (session.getBeatsPerBar() == 3);
    const auto& a = session.getTracks()[0];
    CHECK (a.clips[0].timelineStart == 192000);
    CHECK (a.clips[0].length == 4800);                 // audio content keeps its length (loops are re-conformed by the app)
    CHECK (a.alternates[0].clips[0].timelineStart == 96000);
    CHECK (a.automation[0]->points[1].time == 192000);
    const auto& k = session.getTracks()[1];
    CHECK (k.midiClips[0].timelineStart == 384000);
    CHECK (k.midiClips[0].length == 192000);
    CHECK (k.midiClips[0].loopOffset == 9600);
    CHECK_THAT (session.getMarkers()[0].seconds, WithinAbs (8.0, 1e-9));
    CHECK_THAT (session.getMarkers()[0].endSeconds, WithinAbs (12.0, 1e-9));
    CHECK (session.getHistory().getUndoName() == "Set Tempo");

    session.undo();
    CHECK (session.getBpm() == 120.0);
    CHECK (session.getBeatsPerBar() == 4);
    CHECK (session.getTracks()[0].clips[0].timelineStart == 96000);
    CHECK (session.getTracks()[0].automation[0]->points[1].time == 96000);
    CHECK (session.getTracks()[1].midiClips[0].length == 96000);
    CHECK_THAT (session.getMarkers()[0].seconds, WithinAbs (4.0, 1e-9));
    session.redo();
    CHECK (session.getTracks()[1].midiClips[0].timelineStart == 384000);

    // Without follow, only the numbers change
    session.undo();
    session.execute (std::make_unique<model::SetTempoCommand> (240.0, 4, false));
    CHECK (session.getBpm() == 240.0);
    CHECK (session.getTracks()[0].clips[0].timelineStart == 96000);
    CHECK_THAT (session.getMarkers()[0].seconds, WithinAbs (4.0, 1e-9));
    // Clamped range
    session.execute (std::make_unique<model::SetTempoCommand> (1000.0, 0, false));
    CHECK (session.getBpm() == 400.0);
    CHECK (session.getBeatsPerBar() == 1);
}
