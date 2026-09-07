#include <catch2/catch_test_macros.hpp>
#include <ClipLoop.h>
#include <Session.h>

using namespace beatmaker;

static void fillSession (model::Session& s, double sr = 48000.0)
{
    // 120 BPM: a beat is 24000 samples
    model::Track t; t.name = "Keys"; t.type = model::Track::Type::instrument; t.instrumentKind = model::Track::InstrumentKind::synth;
    model::MidiClip a; a.name = "A"; a.sampleRate = sr; a.timelineStart = 0; a.length = 8 * 24000;   // two bars of an 8-beat sequence
    auto seq = std::make_shared<engine::MidiSequence>(); seq->lengthBeats = 8.0; a.sequence = seq;
    model::MidiClip b = a; b.name = "B"; b.timelineStart = 20 * 24000;   // starts 20 beats in
    t.midiClips = { a, b };
    s.execute (std::make_unique<model::AddTrackCommand> (t));
}

TEST_CASE ("Loop on extends a clip by its content, stopping at the next clip; off restores it")
{
    model::Session s; fillSession (s);
    const model::ClipRef a { 0, model::ClipRef::Kind::midi, 0 }, b { 0, model::ClipRef::Kind::midi, 1 };
    CHECK (model::ClipLoop::contentLength (s, a) == 8 * 24000);
    CHECK (model::ClipLoop::nextClipStart (s, a) == 20 * 24000);
    CHECK (model::ClipLoop::nextClipStart (s, b) == -1);
    CHECK (! model::ClipLoop::isLooping (s, a));

    s.execute (model::ClipLoop::setLoop (s, a, true));
    CHECK (model::ClipLoop::isLooping (s, a));
    CHECK (s.getTracks()[0].midiClips[0].length == 16 * 24000);   // doubled: room enough before B at 20 beats

    s.execute (model::ClipLoop::setLoop (s, b, true));            // nothing after B: doubles freely
    CHECK (s.getTracks()[0].midiClips[1].length == 16 * 24000);

    s.execute (model::ClipLoop::setLoop (s, a, false));           // back to its original size
    CHECK (! model::ClipLoop::isLooping (s, a));
    CHECK (s.getTracks()[0].midiClips[0].length == 8 * 24000);
    s.undo();
    CHECK (model::ClipLoop::isLooping (s, a));
    CHECK (s.getTracks()[0].midiClips[0].length == 16 * 24000);

    // Not enough room: A moved so B is only 10 beats away -> extends to the gap
    s.execute (std::make_unique<model::MoveClipCommand> (a, 0, 10 * 24000));
    s.execute (model::ClipLoop::setLoop (s, a, false));
    s.execute (model::ClipLoop::setLoop (s, a, true));
    CHECK (s.getTracks()[0].midiClips[0].length == 10 * 24000);
}

TEST_CASE ("Trimming a looping clip back to its content switches the loop off")
{
    model::Session s; fillSession (s);
    const model::ClipRef a { 0, model::ClipRef::Kind::midi, 0 };
    s.execute (model::ClipLoop::setLoop (s, a, true));
    CHECK (model::ClipLoop::afterTrim (s, a) == nullptr);                       // still longer than its content
    s.execute (std::make_unique<model::TrimClipCommand> (a, 0, 12 * 24000));
    CHECK (model::ClipLoop::afterTrim (s, a) == nullptr);
    s.execute (std::make_unique<model::TrimClipCommand> (a, 0, 8 * 24000));
    auto off = model::ClipLoop::afterTrim (s, a);
    REQUIRE (off != nullptr);
    s.execute (std::move (off));
    CHECK (! model::ClipLoop::isLooping (s, a));
    CHECK (model::ClipLoop::afterTrim (s, a) == nullptr);
    CHECK (model::ClipLoop::setLoop (s, { 0, model::ClipRef::Kind::audio, 0 }, true) == nullptr);   // audio clips have no loop
}
