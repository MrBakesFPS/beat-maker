#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <LuaEngine.h>
#include <Session.h>

using namespace beatmaker;
using Catch::Matchers::WithinAbs;

namespace
{
    struct FakeHost final : scripting::ScriptHost
    {
        model::Session s;
        engine::Transport t;
        juce::StringArray logs, statuses, commandsRun, registered;
        std::function<void()> lastRegistered;
        juce::var prefValue;
        FakeHost() { t.setSampleRate (48000.0); t.setBpm (120.0); }
        model::Session& session() override { return s; }
        engine::Transport& transport() override { return t; }
        double sampleRate() const override { return 48000.0; }
        int addTrack (const juce::String& kind, const juce::String& name) override
        {
            model::Track track; track.name = name.isNotEmpty() ? name : kind;
            if (kind == "audio") track.type = model::Track::Type::audio;
            else if (kind == "synth") { track.type = model::Track::Type::instrument; track.instrumentKind = model::Track::InstrumentKind::synth;
                                        track.instrument = engine::Instrument::create (engine::InstrumentType::subtractive, 48000.0);
                                        track.instrumentParams = std::make_shared<const engine::InstrumentParams> (engine::Instrument::defaultParams (engine::InstrumentType::subtractive));
                                        model::MidiClip c; c.sampleRate = 48000.0; c.length = 96000; c.sequence = std::make_shared<const engine::MidiSequence>();
                                        s.execute (std::make_unique<model::AddTrackCommand> (track)); s.execute (std::make_unique<model::AddMidiClipCommand> (s.getNumTracks() - 1, c)); return s.getNumTracks() - 1; }
            else if (kind == "aux") track.type = model::Track::Type::aux;
            else return -1;
            s.execute (std::make_unique<model::AddTrackCommand> (track));
            return s.getNumTracks() - 1;
        }
        void status (const juce::String& m) override { statuses.add (m); }
        void log (const juce::String& m) override { logs.add (m); }
        bool runCommand (const juce::String& id) override { commandsRun.add (id); return id != "nope"; }
        juce::String bounce (const juce::File&) override { return {}; }
        bool saveSession (const juce::File&) override { return true; }
        bool openSession (const juce::File&) override { return false; }
        juce::var getPreference (const juce::String&) override { return prefValue; }
        void setPreference (const juce::String&, const juce::var& v) override { prefValue = v; }
        void registerCommand (const juce::String& id, const juce::String&, std::function<void()> run) override { registered.add (id); lastRegistered = std::move (run); }
    };
}

TEST_CASE ("Lua scripts drive the session through undoable commands and print to the host")
{
    FakeHost host;
    scripting::LuaEngine lua (host);

    auto r = lua.run ("print('hello', 1 + 1, beatmaker.version)");
    CHECK (r.ok);
    CHECK (r.output == "hello\t2\t0.1\n");
    CHECK (host.logs[0] == "hello\t2\t0.1");

    r = lua.run (R"(
        local s = beatmaker.session
        local a = s.add_track('audio', 'Vox')
        local b = s.add_track('synth', 'Keys')
        s.set_gain_db(a, -6)
        s.set_pan(a, -0.5)
        s.set_mute(b, true)
        s.rename_track(b, 'Piano')
        print(s.num_tracks(), s.track(a).name, s.track(a).kind, s.track(b).kind)
        for _, t in ipairs(s.tracks()) do print(t.index, t.name) end
    )");
    INFO (r.error);
    REQUIRE (r.ok);
    CHECK (host.s.getNumTracks() == 2);
    CHECK_THAT (juce::Decibels::gainToDecibels (host.s.getTracks()[0].gain), WithinAbs (-6.0, 0.01));
    CHECK_THAT (host.s.getTracks()[0].pan, WithinAbs (-0.5f, 1e-6));
    CHECK (host.s.getTracks()[1].mute);
    CHECK (host.s.getTracks()[1].name == "Piano");
    CHECK (r.output.startsWith ("2\tVox\taudio\tsynth\n1\tVox\n2\tPiano\n"));
    CHECK (host.s.getHistory().getUndoName() == "Rename Track");
    host.s.undo();
    CHECK (host.s.getTracks()[1].name == "Keys");

    // MIDI notes
    r = lua.run (R"(
        local s = beatmaker.session
        s.set_midi_notes(2, 1, { {pitch=60, velocity=100, start=0, length=1}, {pitch=64, start=1}, {pitch=67, velocity=90, start=2, length=0.5} })
        s.add_midi_note(2, 1, 72, 80, 3, 1)
        local notes = s.midi_notes(2, 1)
        print(#notes, notes[1].pitch, notes[4].pitch, notes[3].length)
        local clips = s.clips(2)
        print(#clips, clips[1].kind, clips[1].length)
    )");
    INFO (r.error);
    REQUIRE (r.ok);
    CHECK (r.output == "4\t60\t72\t0.5\n1\tmidi\t2.0\n");
    CHECK (host.s.getTracks()[1].midiClips[0].sequence->notes.size() == 4);

    // Markers, inserts, sends, transport
    r = lua.run (R"(
        local s, t = beatmaker.session, beatmaker.transport
        local id = s.add_marker('Verse', 8)
        s.add_marker('Chorus', 16, 24)
        s.set_insert(1, 1, 'Compressor')
        s.set_insert_param(1, 1, 'Threshold', -30)
        s.set_insert(0, 1, 'Limiter')
        s.set_send(1, 1, 3, 0.5, true)
        t.set_bpm(90); t.locate_bar(3)
        print(#s.markers(), s.markers()[2].is_section, s.insert_params(1, 1)[1].value, t.bpm(), string.format('%.3f', t.position()), t.bar())
        s.remove_marker(id)
        print(#s.markers())
    )");
    INFO (r.error);
    REQUIRE (r.ok);
    CHECK (r.output == "2\ttrue\t-30.0\t90.0\t5.333\t3\n1\n");
    CHECK (host.s.getTracks()[0].inserts[0].type == engine::EffectType::compressor);
    CHECK (host.s.getMaster().inserts[0].type == engine::EffectType::limiter);
    CHECK (host.s.getTracks()[0].sends[0].bus == 2);
    CHECK (host.s.getTracks()[0].sends[0].preFader);

    // App services and registered commands
    r = lua.run (R"(
        local app = beatmaker.app
        app.status('done'); app.set_pref('x', 42); print(app.pref('x'), app.run_command('transport.play'), app.run_command('nope'))
        app.register_command('script.hello', 'Say hello', function() print('hi from lua'); app.status('hello ran') end)
    )");
    INFO (r.error);
    REQUIRE (r.ok);
    CHECK (host.statuses[0] == "done");
    CHECK ((double) host.prefValue == 42.0);
    CHECK (r.output == "42.0\ttrue\tfalse\n");
    REQUIRE (host.registered.contains ("script.hello"));
    host.lastRegistered();
    CHECK (host.statuses.contains ("hello ran"));
    CHECK (host.logs.contains ("hi from lua"));

    // Errors carry the line; bad indices are errors, not crashes; the sandbox has no io/os.exit
    r = lua.run ("local x = 1\nlocal y = nil + 1");
    CHECK_FALSE (r.ok);
    CHECK (r.error.contains (":2:"));
    r = lua.run ("beatmaker.session.set_gain(99, 1)");
    CHECK_FALSE (r.ok);
    CHECK (r.error.contains ("no track 99"));
    r = lua.run ("print(io == nil, os.exit == nil, os.time() > 0, dofile == nil)");
    CHECK (r.ok);
    CHECK (r.output == "true\ttrue\ttrue\ttrue\n");
    r = lua.run ("beatmaker.session.add_track('bagpipes')");
    CHECK_FALSE (r.ok);
    CHECK (r.error.contains ("unknown track kind"));
    CHECK (scripting::LuaEngine::apiReference().contains ("add_midi_note"));
}
