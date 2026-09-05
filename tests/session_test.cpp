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

    // Clip gain stays on the clip; the track fader lives on the strip.
    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->clips.size() == 3);
    REQUIRE (snap->strips.size() == 3);
    CHECK (snap->clips[0].gain == 2.0f);
    CHECK (snap->clips[0].strip == 0);
    CHECK (snap->clips[2].strip == 2);
    CHECK (snap->strips[0].gain == 0.5f);
    CHECK_FALSE (snap->strips[0].muted);

    // Mute and solo are resolved into the strip's muted flag; clips stay listed.
    s.execute (std::make_unique<SetTrackFlagCommand> (1, SetTrackFlagCommand::Flag::mute, true));
    snap = buildRenderSnapshot (s);
    CHECK (snap->clips.size() == 3);
    CHECK (snap->strips[1].muted);
    CHECK_FALSE (snap->strips[0].muted);

    s.execute (std::make_unique<SetTrackFlagCommand> (2, SetTrackFlagCommand::Flag::solo, true));
    snap = buildRenderSnapshot (s);
    CHECK (snap->strips[0].muted);          // not soloed
    CHECK (snap->strips[1].muted);          // muted
    CHECK_FALSE (snap->strips[2].muted);    // the soloed track

    s.undo(); s.undo();
    snap = buildRenderSnapshot (s);
    for (const auto& strip : snap->strips) CHECK_FALSE (strip.muted);
}

TEST_CASE ("Tracks get stable increasing ids that survive undo/redo")
{
    Session s;
    Track a; a.name = "A";
    Track b; b.name = "B";
    s.execute (std::make_unique<AddTrackCommand> (a));
    s.execute (std::make_unique<AddTrackCommand> (b));
    CHECK (s.getTracks()[0].id == 1);
    CHECK (s.getTracks()[1].id == 2);

    s.undo();
    s.redo();
    CHECK (s.getTracks()[1].id == 2);
    CHECK (s.indexOfTrackId (2) == 1);
    CHECK (s.indexOfTrackId (99) == -1);
}

TEST_CASE ("Step edits are copy-on-write and undoable")
{
    Session s;
    Track t; t.type = Track::Type::instrument;
    t.drumKit = std::make_shared<const beatmaker::engine::DrumKit>();
    s.execute (std::make_unique<AddTrackCommand> (t));

    PatternClip clip;
    clip.pattern = std::make_shared<const beatmaker::engine::StepPattern>();
    clip.sampleRate = 44100.0;
    clip.length = 44100 * 8;
    s.execute (std::make_unique<AddPatternClipCommand> (0, clip));
    CHECK (s.getLengthSeconds() == 8.0);

    auto original = s.getTracks()[0].patternClips[0].pattern;
    s.execute (std::make_unique<SetStepCommand> (0, 0, 3, 7, 100));

    auto edited = s.getTracks()[0].patternClips[0].pattern;
    CHECK (edited != original);                 // new object
    CHECK (original->get (3, 7) == 0);          // old untouched (engine may still read it)
    CHECK (edited->get (3, 7) == 100);

    s.undo();
    CHECK (s.getTracks()[0].patternClips[0].pattern == original);

    // Muted instrument tracks keep their kit in the snapshot but never fire.
    s.redo();
    s.execute (std::make_unique<SetTrackFlagCommand> (0, SetTrackFlagCommand::Flag::mute, true));
    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->patterns.size() == 1);
    CHECK (snap->patterns[0].length == 0);
    CHECK (snap->patterns[0].kit == t.drumKit);
}

TEST_CASE ("Replacing a pad sample creates a new kit and is undoable")
{
    Session s;
    Track t; t.type = Track::Type::instrument;
    t.drumKit = std::make_shared<const beatmaker::engine::DrumKit>();
    s.execute (std::make_unique<AddTrackCommand> (t));

    beatmaker::engine::DrumSample sample;
    sample.name = "Custom";
    sample.audio = std::make_shared<const juce::AudioBuffer<float>> (1, 10);
    s.execute (std::make_unique<SetPadSampleCommand> (0, 5, sample));

    auto& kit = s.getTracks()[0].drumKit;
    CHECK (kit != t.drumKit);
    CHECK (kit->pads[5].name == "Custom");
    CHECK (kit->pads[5].audio == sample.audio);

    s.undo();
    CHECK (s.getTracks()[0].drumKit == t.drumKit);
}

TEST_CASE ("Arm, monitor and input changes apply but are not undoable")
{
    Session s;
    Track t; t.name = "Vox";
    s.execute (std::make_unique<AddTrackCommand> (t));
    CHECK (s.getHistory().canUndo());
    CHECK (s.getHistory().getUndoName() == "Add Track");

    s.execute (std::make_unique<SetTrackFlagCommand> (0, SetTrackFlagCommand::Flag::arm, true));
    s.execute (std::make_unique<SetTrackFlagCommand> (0, SetTrackFlagCommand::Flag::monitor, true));
    s.execute (std::make_unique<SetTrackInputCommand> (0, 3, 2));

    const auto& track = s.getTracks()[0];
    CHECK (track.armed);
    CHECK (track.monitor);
    CHECK (track.firstInput == 3);
    CHECK (track.numInputs == 2);
    CHECK (s.getHistory().getUndoName() == "Add Track");   // history untouched

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->monitors.size() == 1);
    CHECK (snap->monitors[0].firstInput == 3);
    CHECK (snap->monitors[0].numInputs == 2);

    s.execute (std::make_unique<SetTrackFlagCommand> (0, SetTrackFlagCommand::Flag::mute, true));
    CHECK (buildRenderSnapshot (s)->monitors.empty());     // muted tracks don't monitor
    CHECK (s.getHistory().getUndoName() == "Mute");
}

TEST_CASE ("Synth tracks: MIDI clips, copy-on-write sequences and sound changes are undoable")
{
    using beatmaker::engine::MidiSequence;
    using beatmaker::engine::Instrument;
    using beatmaker::engine::InstrumentType;

    Session s;
    Track t; t.type = Track::Type::instrument; t.instrumentKind = Track::InstrumentKind::synth;
    t.instrument = Instrument::create (InstrumentType::subtractive, 48000.0);
    const auto presets = Instrument::presets (InstrumentType::subtractive);
    t.instrumentParams = std::make_shared<const beatmaker::engine::InstrumentParams> (presets[0]);
    s.execute (std::make_unique<AddTrackCommand> (t));
    CHECK (s.getTracks()[0].isSynth());
    CHECK_FALSE (s.getTracks()[0].isDrumMachine());

    MidiClip clip;
    clip.sequence = std::make_shared<const MidiSequence> (MidiSequence::createDefaultArpeggio());
    clip.sampleRate = 48000.0;
    clip.length = 48000 * 8;
    s.execute (std::make_unique<AddMidiClipCommand> (0, clip));
    REQUIRE (s.getTracks()[0].midiClips.size() == 1);
    CHECK (s.getLengthSeconds() == 8.0);

    auto original = s.getTracks()[0].midiClips[0].sequence;
    auto edited = std::make_shared<MidiSequence> (*original);
    edited->notes.push_back ({ 72, 100, 2.0, 1.0 });
    s.execute (std::make_unique<ReplaceMidiSequenceCommand> (0, 0, edited, "Add Note"));
    CHECK (s.getHistory().getUndoName() == "Add Note");
    CHECK (s.getTracks()[0].midiClips[0].sequence == edited);
    CHECK (original->notes.size() == 16);
    s.undo();
    CHECK (s.getTracks()[0].midiClips[0].sequence == original);

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->instruments.size() == 1);
    CHECK (snap->instruments[0].instrumentId == s.getTracks()[0].id);
    CHECK (snap->instruments[0].instance == s.getTracks()[0].instrument);
    REQUIRE (snap->midiClips.size() == 1);
    CHECK (snap->midiClips[0].length == 48000 * 8);

    s.execute (std::make_unique<SetInstrumentParamsCommand> (0, std::make_shared<const beatmaker::engine::InstrumentParams> (presets[2])));
    CHECK (s.getTracks()[0].instrumentParams->presetName == "Soft Pad");
    s.undo();
    CHECK (s.getTracks()[0].instrumentParams->presetName == "Init Saw");

    // Switching instruments swaps the instance; undo brings the old one back.
    auto oldInstance = s.getTracks()[0].instrument;
    std::shared_ptr<Instrument> fm = Instrument::create (InstrumentType::fm, 48000.0);
    s.execute (std::make_unique<SetInstrumentCommand> (0, fm, std::make_shared<const beatmaker::engine::InstrumentParams> (Instrument::defaultParams (InstrumentType::fm))));
    CHECK (s.getTracks()[0].instrumentType() == InstrumentType::fm);
    CHECK (s.getTracks()[0].instrument == fm);
    s.undo();
    CHECK (s.getTracks()[0].instrument == oldInstance);
    CHECK (s.getTracks()[0].instrumentType() == InstrumentType::subtractive);

    s.execute (std::make_unique<SetTrackFlagCommand> (0, SetTrackFlagCommand::Flag::mute, true));
    snap = buildRenderSnapshot (s);
    REQUIRE (snap->instruments.size() == 1);       // instrument kept for auditioning
    CHECK (snap->midiClips[0].length == 0);        // but nothing plays
}
