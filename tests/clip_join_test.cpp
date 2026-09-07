#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ClipJoin.h>
#include <Session.h>

using namespace beatmaker;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Joining MIDI clips writes their notes out, loops included, with the gap as silence")
{
    model::Session s;   // 120 BPM, a beat = 24000 samples at 48k
    const double sr = 48000.0;
    model::Track t; t.name = "Keys"; t.type = model::Track::Type::instrument; t.instrumentKind = model::Track::InstrumentKind::synth;
    model::MidiClip a; a.name = "A"; a.sampleRate = sr; a.timelineStart = 0; a.length = 8 * 24000; a.loop = true;   // 4-beat sequence looped twice
    auto seqA = std::make_shared<engine::MidiSequence>(); seqA->lengthBeats = 4.0; seqA->notes = { { 60, 100, 0.0, 1.0 }, { 64, 90, 2.0, 0.5 } }; a.sequence = seqA;
    model::MidiClip b; b.name = "B"; b.sampleRate = sr; b.timelineStart = 12 * 24000; b.length = 4 * 24000; b.loop = false;   // starts after a 4-beat gap
    auto seqB = std::make_shared<engine::MidiSequence>(); seqB->lengthBeats = 4.0; seqB->notes = { { 67, 80, 1.0, 1.0 } }; b.sequence = seqB;
    t.midiClips = { a, b };
    s.execute (std::make_unique<model::AddTrackCommand> (t));
    const std::vector<model::ClipRef> refs { { 0, model::ClipRef::Kind::midi, 1 }, { 0, model::ClipRef::Kind::midi, 0 } };
    juce::String reason;
    CHECK (model::ClipJoin::canJoin (s, refs, reason));
    auto cmd = std::make_unique<model::JoinClipsCommand> (s, refs);
    REQUIRE (! cmd->wasCancelled());
    s.execute (std::move (cmd));
    REQUIRE (s.getTracks()[0].midiClips.size() == 1);
    const auto& j = s.getTracks()[0].midiClips[0];
    CHECK (j.name == "A");
    CHECK (j.timelineStart == 0); CHECK (j.length == 16 * 24000); CHECK (! j.loop); CHECK (j.loopOffset == 0);
    REQUIRE (j.sequence != nullptr);
    CHECK_THAT (j.sequence->lengthBeats, WithinAbs (16.0, 1e-9));
    REQUIRE (j.sequence->notes.size() == 5);   // 2 notes x 2 passes + 1
    CHECK_THAT (j.sequence->notes[0].startBeat, WithinAbs (0.0, 1e-9));
    CHECK_THAT (j.sequence->notes[1].startBeat, WithinAbs (2.0, 1e-9));
    CHECK_THAT (j.sequence->notes[2].startBeat, WithinAbs (4.0, 1e-9));
    CHECK_THAT (j.sequence->notes[3].startBeat, WithinAbs (6.0, 1e-9));
    CHECK_THAT (j.sequence->notes[4].startBeat, WithinAbs (13.0, 1e-9));   // B's note: 12 + 1
    CHECK (j.sequence->notes[4].pitch == 67);
    s.undo();
    CHECK (s.getTracks()[0].midiClips.size() == 2);
    s.redo();
    CHECK (s.getTracks()[0].midiClips.size() == 1);
}

TEST_CASE ("Joining pattern clips merges their steps; audio joins only adjacent pieces of one file")
{
    model::Session s;
    const double sr = 48000.0;
    model::Track t; t.name = "Drums"; t.type = model::Track::Type::instrument; t.instrumentKind = model::Track::InstrumentKind::drumMachine;
    model::PatternClip a; a.sampleRate = sr; a.timelineStart = 0; a.length = 4 * 24000; a.loop = false;
    auto pa = std::make_shared<engine::StepPattern>(); pa->numSteps = 16; pa->set (0, 0, 120); pa->set (1, 8, 90); a.pattern = pa;
    model::PatternClip b = a; b.timelineStart = 8 * 24000; auto pb = std::make_shared<engine::StepPattern>(); pb->numSteps = 16; pb->set (4, 2, 60); b.pattern = pb;
    t.patternClips = { a, b };
    s.execute (std::make_unique<model::AddTrackCommand> (t));
    auto cmd = std::make_unique<model::JoinClipsCommand> (s, std::vector<model::ClipRef> { { 0, model::ClipRef::Kind::pattern, 0 }, { 0, model::ClipRef::Kind::pattern, 1 } });
    REQUIRE (! cmd->wasCancelled());
    s.execute (std::move (cmd));
    REQUIRE (s.getTracks()[0].patternClips.size() == 1);
    const auto& j = s.getTracks()[0].patternClips[0];
    CHECK (j.length == 12 * 24000);
    REQUIRE (j.pattern != nullptr);
    CHECK (j.pattern->numSteps == 48);
    CHECK (j.pattern->get (0, 0) == 120); CHECK (j.pattern->get (1, 8) == 90); CHECK (j.pattern->get (4, 34) == 60);   // B's hit at 8 beats * 4 + 2
    CHECK (j.pattern->get (0, 16) == 0);   // the gap

    // Audio: two adjacent pieces of one buffer heal; a gap refuses
    model::Track at; at.name = "Vox"; at.type = model::Track::Type::audio;
    auto buf = std::make_shared<juce::AudioBuffer<float>> (1, 96000);
    model::AudioClip x; x.audio = buf; x.sampleRate = sr; x.timelineStart = 1000; x.sourceOffset = 0; x.length = 40000;
    model::AudioClip y = x; y.timelineStart = 41000; y.sourceOffset = 40000; y.length = 30000;
    model::AudioClip z = x; z.timelineStart = 90000; z.sourceOffset = 70000; z.length = 20000;
    at.clips = { x, y, z };
    s.execute (std::make_unique<model::AddTrackCommand> (at));
    juce::String reason;
    CHECK (model::ClipJoin::canJoin (s, { { 1, model::ClipRef::Kind::audio, 0 }, { 1, model::ClipRef::Kind::audio, 1 } }, reason));
    CHECK (! model::ClipJoin::canJoin (s, { { 1, model::ClipRef::Kind::audio, 1 }, { 1, model::ClipRef::Kind::audio, 2 } }, reason));
    CHECK (reason.contains ("adjacent"));
    CHECK (! model::ClipJoin::canJoin (s, { { 0, model::ClipRef::Kind::pattern, 0 }, { 1, model::ClipRef::Kind::audio, 0 } }, reason));
    s.execute (std::make_unique<model::JoinClipsCommand> (s, std::vector<model::ClipRef> { { 1, model::ClipRef::Kind::audio, 0 }, { 1, model::ClipRef::Kind::audio, 1 } }));
    REQUIRE (s.getTracks()[1].clips.size() == 2);
    bool found = false;
    for (const auto& c : s.getTracks()[1].clips) if (c.timelineStart == 1000) { found = true; CHECK (c.length == 70000); CHECK (c.sourceOffset == 0); }
    CHECK (found);
    const auto groups = model::ClipJoin::groups (s, { { 1, model::ClipRef::Kind::audio, 0 }, { 0, model::ClipRef::Kind::pattern, 0 }, { 1, model::ClipRef::Kind::audio, 1 } });
    CHECK (groups.size() == 2);
}
