#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <NoteEdits.h>

using namespace beatmaker;
using model::NoteEdits;
using Catch::Matchers::WithinAbs;

static engine::MidiSequence makeSeq()
{
    engine::MidiSequence s; s.lengthBeats = 8.0;
    s.notes = { { 60, 100, 0.0, 1.0 }, { 64, 90, 1.1, 0.5 }, { 67, 80, 2.0, 1.0 }, { 72, 70, 4.3, 0.4 } };
    s.sortNotes();
    return s;
}

TEST_CASE ("NoteEdits: region selection, keys, quantize, transpose, nudge, velocity")
{
    auto s = makeSeq();
    CHECK (NoteEdits::all (s).size() == 4);
    CHECK (NoteEdits::inRegion (s, 0.5, 2.5, 60, 70) == std::vector<int> { 0, 1, 2 });   // overlap counts, pitch range inclusive
    CHECK (NoteEdits::inRegion (s, 2.5, 0.5, 70, 60) == std::vector<int> { 0, 1, 2 });   // either order
    CHECK (NoteEdits::inRegion (s, 0.0, 8.0, 72, 72) == std::vector<int> { 3 });
    const auto keys = NoteEdits::keysOf (s, { 1, 3 });
    CHECK (NoteEdits::resolve (s, keys) == std::vector<int> { 1, 3 });

    auto q = NoteEdits::quantize (s, { 1, 3 }, 0.5);
    CHECK_THAT (q.notes[1].startBeat, WithinAbs (1.0, 1e-9));
    CHECK_THAT (q.notes[3].startBeat, WithinAbs (4.5, 1e-9));
    CHECK_THAT (q.notes[3].lengthBeats, WithinAbs (0.4, 1e-9));   // lengths untouched
    auto qh = NoteEdits::quantize (s, { 1 }, 0.5, 0.5f);           // half strength
    CHECK_THAT (qh.notes[1].startBeat, WithinAbs (1.05, 1e-9));
    auto ql = NoteEdits::quantize (s, { 3 }, 0.5, 1.0f, true);
    CHECK_THAT (ql.notes[3].lengthBeats, WithinAbs (0.5, 1e-9));

    auto t = NoteEdits::transpose (s, { 0, 2 }, 12);
    CHECK (t.notes[0].pitch == 72); CHECK (t.notes[2].pitch == 79); CHECK (t.notes[1].pitch == 64);
    auto tc = NoteEdits::transpose (s, { 3 }, 100);
    CHECK (tc.notes[3].pitch == 127);

    auto n = NoteEdits::nudge (s, { 0, 1 }, -0.5);                 // clamped: the group can move only to 0
    CHECK_THAT (n.notes[0].startBeat, WithinAbs (0.0, 1e-9));
    CHECK_THAT (n.notes[1].startBeat, WithinAbs (1.1, 1e-9));
    auto n2 = NoteEdits::nudge (s, { 3 }, 10.0);                   // stays inside the sequence
    CHECK_THAT (n2.notes[3].getEndBeat(), WithinAbs (8.0, 1e-9));

    auto v = NoteEdits::changeVelocity (s, { 0, 1 }, 40);
    CHECK (v.notes[0].velocity == 127); CHECK (v.notes[1].velocity == 127); CHECK (v.notes[2].velocity == 80);
    auto sv = NoteEdits::setVelocity (s, { 2 }, 0);
    CHECK (sv.notes[2].velocity == 1);
    auto sl = NoteEdits::setLength (s, { 2 }, 20.0);
    CHECK_THAT (sl.notes[2].lengthBeats, WithinAbs (6.0, 1e-9));
}

TEST_CASE ("NoteEdits: remove, copy, paste, duplicate, legato")
{
    auto s = makeSeq();
    auto r = NoteEdits::remove (s, { 1, 2 });
    REQUIRE (r.notes.size() == 2);
    CHECK (r.notes[0].pitch == 60); CHECK (r.notes[1].pitch == 72);

    const auto clip = NoteEdits::copy (s, { 2, 0 });
    REQUIRE (clip.size() == 2);
    CHECK (clip[0].pitch == 60);                                    // sorted by start
    std::vector<model::NoteKey> keys;
    auto p = NoteEdits::paste (s, clip, 5.0, &keys);
    REQUIRE (p.notes.size() == 6);
    REQUIRE (keys.size() == 2);
    CHECK_THAT (keys[0].startBeat, WithinAbs (5.0, 1e-9));
    CHECK_THAT (keys[1].startBeat, WithinAbs (7.0, 1e-9));
    auto p2 = NoteEdits::paste (s, clip, 7.5);                      // the second note would start past the end: dropped, first shortened
    REQUIRE (p2.notes.size() == 5);
    CHECK_THAT (p2.notes.back().getEndBeat(), WithinAbs (8.0, 1e-9));

    std::vector<model::NoteKey> dupKeys;
    auto d = NoteEdits::duplicate (s, { 0, 1 }, &dupKeys);          // selection spans 0..1.6 -> copies start at 1.6
    REQUIRE (d.notes.size() == 6);
    REQUIRE (dupKeys.size() == 2);
    CHECK_THAT (dupKeys[0].startBeat, WithinAbs (1.6, 1e-9));
    CHECK_THAT (dupKeys[1].startBeat, WithinAbs (2.7, 1e-9));

    auto l = NoteEdits::legato (s, NoteEdits::all (s));
    CHECK_THAT (l.notes[0].lengthBeats, WithinAbs (1.1, 1e-9));
    CHECK_THAT (l.notes[1].lengthBeats, WithinAbs (0.9, 1e-9));
    CHECK_THAT (l.notes[3].lengthBeats, WithinAbs (3.7, 1e-9));   // last note runs to the end
    CHECK_THAT (NoteEdits::snap (1.1, 0.25), WithinAbs (1.0, 1e-9));
}
