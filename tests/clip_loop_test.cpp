#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
using Catch::Matchers::WithinAbs;
#include <ClipLoop.h>
#include <Session.h>
#include <StepEdits.h>

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

TEST_CASE ("A clip extended, edited beyond its notes, then shortened, loops from its original length again")
{
    model::Session s; fillSession (s);
    const model::ClipRef a { 0, model::ClipRef::Kind::midi, 0 };
    s.execute (model::ClipLoop::setLoop (s, a, true));                     // 8 -> 16 beats, base 8 remembered
    CHECK_THAT (model::ClipLoop::baseBeats (s, a), WithinAbs (8.0, 1e-9));
    // The editor unrolls the loop to add a note in the second pass, then the note goes away again
    auto grown = std::make_shared<engine::MidiSequence> (*s.getTracks()[0].midiClips[0].sequence);
    grown->lengthBeats = 16.0; grown->notes.push_back ({ 60, 100, 10.0, 1.0 });
    s.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (0, 0, grown, "Unroll"));
    auto emptied = std::make_shared<engine::MidiSequence> (*grown); emptied->notes.clear();
    s.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (0, 0, emptied, "Delete Note"));
    CHECK_THAT (model::ClipLoop::contentBeats (s, a), WithinAbs (16.0, 1e-9));
    // Shortened back to two bars: loop off and the content returns to eight beats
    s.execute (std::make_unique<model::TrimClipCommand> (a, 0, 8 * 24000));
    auto off = model::ClipLoop::afterTrim (s, a);
    REQUIRE (off != nullptr);
    s.execute (std::move (off));
    CHECK (! model::ClipLoop::isLooping (s, a));
    CHECK_THAT (model::ClipLoop::contentBeats (s, a), WithinAbs (8.0, 1e-9));
    CHECK_THAT (model::ClipLoop::baseBeats (s, a), WithinAbs (8.0, 1e-9));   // forgotten base falls back to the content
    // Loop again: one extra pass of the original length, not of the grown one
    s.execute (model::ClipLoop::setLoop (s, a, true));
    CHECK (s.getTracks()[0].midiClips[0].length == 16 * 24000);
    // A note left past the base keeps the grown content on Loop off (nothing is lost)
    auto kept = std::make_shared<engine::MidiSequence> (*s.getTracks()[0].midiClips[0].sequence);
    kept->lengthBeats = 16.0; kept->notes.push_back ({ 62, 100, 12.0, 1.0 });
    s.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (0, 0, kept, "Unroll"));
    s.execute (model::ClipLoop::setLoop (s, a, false));
    CHECK_THAT (model::ClipLoop::contentBeats (s, a), WithinAbs (16.0, 1e-9));
    CHECK (s.getTracks()[0].midiClips[0].length == 8 * 24000);   // the clip itself returns to its base length
}

TEST_CASE ("An unlooped clip extended, edited in the empty part, then shrunk, loops from its own length")
{
    model::Session s; fillSession (s);
    const model::ClipRef a { 0, model::ClipRef::Kind::midi, 0 };
    // The clip is dragged out to 12 beats with Loop off; the editor grows the notes to cover it, a note is added and removed
    s.execute (std::make_unique<model::TrimClipCommand> (a, 0, 12 * 24000));
    CHECK (model::ClipLoop::afterTrim (s, a) == nullptr);                  // growing a clip touches nothing
    auto grown = std::make_shared<engine::MidiSequence> (*s.getTracks()[0].midiClips[0].sequence);
    grown->lengthBeats = 12.0; grown->notes.push_back ({ 60, 100, 10.0, 1.0 });
    s.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (0, 0, grown, "Extend Notes"));
    auto emptied = std::make_shared<engine::MidiSequence> (*grown); emptied->notes.clear();
    s.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (0, 0, emptied, "Delete Note"));
    // Shrunk back to 8 beats: the empty growth folds back to the clip
    s.execute (std::make_unique<model::TrimClipCommand> (a, 0, 8 * 24000));
    CHECK_THAT (model::ClipLoop::clipBeats (s, a), WithinAbs (8.0, 1e-9));
    auto fold = model::ClipLoop::afterTrim (s, a);
    REQUIRE (fold != nullptr);
    s.execute (std::move (fold));
    CHECK (! model::ClipLoop::isLooping (s, a));
    CHECK_THAT (model::ClipLoop::contentBeats (s, a), WithinAbs (8.0, 1e-9));
    s.execute (model::ClipLoop::setLoop (s, a, true));                     // the loop starts where the clip ends
    CHECK (s.getTracks()[0].midiClips[0].length == 16 * 24000);
    CHECK_THAT (model::ClipLoop::baseBeats (s, a), WithinAbs (8.0, 1e-9));

    // Even without the trim hook, Loop measures the clip, not stale content
    s.execute (model::ClipLoop::setLoop (s, a, false));
    auto stale = std::make_shared<engine::MidiSequence> (*s.getTracks()[0].midiClips[0].sequence); stale->lengthBeats = 12.0;
    s.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (0, 0, stale, "Stale"));
    s.execute (model::ClipLoop::setLoop (s, a, true));
    CHECK (s.getTracks()[0].midiClips[0].length == 16 * 24000);
    CHECK_THAT (model::ClipLoop::contentBeats (s, a), WithinAbs (8.0, 1e-9));
    s.execute (model::ClipLoop::setLoop (s, a, false));

    // A clip shorter than its notes (4 beats showing, a note at beat 6) keeps its notes; the pass is the content
    auto tail = std::make_shared<engine::MidiSequence> (*s.getTracks()[0].midiClips[0].sequence);
    tail->lengthBeats = 8.0; tail->notes.push_back ({ 64, 100, 6.0, 1.0 });
    s.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (0, 0, tail, "Tail"));
    s.execute (std::make_unique<model::TrimClipCommand> (a, 0, 4 * 24000));
    CHECK (model::ClipLoop::afterTrim (s, a) == nullptr);
    s.execute (model::ClipLoop::setLoop (s, a, true));
    CHECK_THAT (model::ClipLoop::baseBeats (s, a), WithinAbs (8.0, 1e-9));
    CHECK (s.getTracks()[0].midiClips[0].sequence->notes.size() == 1);
    CHECK (s.getTracks()[0].midiClips[0].length == 16 * 24000);

    // A clip longer than its notes (2 beats of notes shown as 8) loops the whole 8 beats, silence included
    model::Session s2; fillSession (s2);
    auto shortSeq = std::make_shared<engine::MidiSequence> (*s2.getTracks()[0].midiClips[0].sequence); shortSeq->lengthBeats = 2.0;
    s2.execute (std::make_unique<model::ReplaceMidiSequenceCommand> (0, 0, shortSeq, "Short"));
    s2.execute (model::ClipLoop::setLoop (s2, a, true));
    CHECK_THAT (model::ClipLoop::contentBeats (s2, a), WithinAbs (8.0, 1e-9));
    CHECK (s2.getTracks()[0].midiClips[0].length == 16 * 24000);
}

TEST_CASE ("An unlooped pattern clip shrunk below its empty growth folds the pattern back")
{
    model::Session s;
    model::Track t; t.name = "Drums"; t.type = model::Track::Type::instrument; t.instrumentKind = model::Track::InstrumentKind::drumMachine;
    model::PatternClip c; c.name = "Beat"; c.sampleRate = 48000.0; c.timelineStart = 0; c.length = 4 * 24000;   // one bar of sixteenths
    auto pat = std::make_shared<engine::StepPattern>(); pat->numSteps = 16; pat->stepsPerBeat = 4; pat->set (0, 0, 100); c.pattern = pat;
    t.patternClips = { c };
    s.execute (std::make_unique<model::AddTrackCommand> (t));
    const model::ClipRef ref { 0, model::ClipRef::Kind::pattern, 0 };
    s.execute (std::make_unique<model::TrimClipCommand> (ref, 0, 8 * 24000));
    s.execute (std::make_unique<model::ReplacePatternCommand> (0, 0, std::make_shared<engine::StepPattern> (model::StepEdits::resize (*pat, 32)), "Extend Pattern"));
    s.execute (std::make_unique<model::TrimClipCommand> (ref, 0, 4 * 24000));
    auto fold = model::ClipLoop::afterTrim (s, ref);
    REQUIRE (fold != nullptr);
    s.execute (std::move (fold));
    CHECK (s.getTracks()[0].patternClips[0].pattern->numSteps == 16);
    CHECK (s.getTracks()[0].patternClips[0].pattern->get (0, 0) == 100);
    s.execute (model::ClipLoop::setLoop (s, ref, true));
    CHECK (s.getTracks()[0].patternClips[0].length == 8 * 24000);
    CHECK_THAT (model::ClipLoop::baseBeats (s, ref), WithinAbs (4.0, 1e-9));
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
