#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <Arrangement.h>
#include <Elastic.h>
#include <MixerCommands.h>
#include <Session.h>
#include <SessionFile.h>
#include <AudioFileLoader.h>
#include <dsp/DrumKitFactory.h>

using namespace beatmaker;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    juce::File writeTone (double hz, double seconds)
    {
        auto file = juce::File::createTempFile (".wav");
        juce::AudioBuffer<float> b (1, (int) (seconds * sr));
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sr));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (1).withBitsPerSample (16)));
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }

    Marker point (const char* name, double seconds, bool section = false, double end = -1.0)
    {
        Marker m; m.name = name; m.seconds = seconds; m.isSection = section; m.endSeconds = end; return m;
    }
}

TEST_CASE ("Memory locations: add, edit, delete, undo; sections are listed in order")
{
    Session s;
    s.execute (std::make_unique<AddMarkerCommand> (point ("Verse", 8.0)));
    s.execute (std::make_unique<AddMarkerCommand> (point ("Intro", 0.0)));
    s.execute (std::make_unique<AddMarkerCommand> (point ("Chorus", 16.0, true, 24.0)));
    REQUIRE (s.getMarkers().size() == 3);
    CHECK (s.getMarkers()[0].name == "Intro");     // kept sorted by time
    CHECK (s.getMarkers()[1].name == "Verse");
    CHECK (s.getMarkers()[0].id == 2);
    CHECK (s.getMarkers()[1].id == 1);
    CHECK (s.getSections().size() == 1);
    CHECK (s.getSections()[0]->name == "Chorus");
    CHECK (s.getHistory().getUndoName() == "Add Section");

    auto edited = *s.getMarker (1);
    edited.name = "Verse 1"; edited.seconds = 4.0; edited.recallSelection = true; edited.selectionStart = 4.0; edited.selectionEnd = 6.0;
    s.execute (std::make_unique<ReplaceMarkerCommand> (edited));
    CHECK (s.getMarker (1)->name == "Verse 1");
    CHECK (s.getMarker (1)->recallSelection);
    s.undo();
    CHECK (s.getMarker (1)->name == "Verse");

    s.execute (std::make_unique<RemoveMarkerCommand> (2));
    CHECK (s.getMarkers().size() == 2);
    CHECK (s.getMarker (2) == nullptr);
    s.undo();
    REQUIRE (s.getMarker (2) != nullptr);
    CHECK (s.getMarkers()[0].name == "Intro");
    s.undo(); s.undo(); s.undo();
    CHECK (s.getMarkers().empty());
}

TEST_CASE ("Arrangement sections move, duplicate and delete their contents across tracks")
{
    Session s;
    Track a; a.name = "A"; a.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (a));
    auto audio = std::make_shared<const juce::AudioBuffer<float>> (1, 48000);
    auto addClip = [&] (const char* name, double start)
    {
        AudioClip c; c.name = name; c.audio = audio; c.sampleRate = sr; c.timelineStart = (juce::int64) (start * sr); c.length = 24000;
        s.execute (std::make_unique<AddClipCommand> (0, c));
    };
    addClip ("intro", 0.0); addClip ("verse1", 2.0); addClip ("verse2", 3.0); addClip ("chorus", 4.0); addClip ("outro", 6.0);
    s.execute (std::make_unique<AddMarkerCommand> (point ("Intro", 0.0, true, 2.0)));
    s.execute (std::make_unique<AddMarkerCommand> (point ("Verse", 2.0, true, 4.0)));
    s.execute (std::make_unique<AddMarkerCommand> (point ("Chorus", 4.0, true, 6.0)));
    const int verseId = s.getSections()[1]->id, chorusId = s.getSections()[2]->id;
    auto startOf = [&] (const char* name) { for (const auto& c : s.getTracks()[0].clips) if (c.name == name) return c.getStartSeconds(); return -1.0; };

    // Chorus before Verse
    auto cmd = Arrangement::moveSection (s, chorusId, false);
    REQUIRE (cmd != nullptr);
    s.execute (std::move (cmd));
    CHECK_THAT (startOf ("chorus"), WithinAbs (2.0, 1e-9));
    CHECK_THAT (startOf ("verse1"), WithinAbs (4.0, 1e-9));
    CHECK_THAT (startOf ("verse2"), WithinAbs (5.0, 1e-9));
    CHECK_THAT (startOf ("outro"), WithinAbs (6.0, 1e-9));
    CHECK (s.getSections()[1]->name == "Chorus");
    CHECK_THAT (s.getSections()[1]->seconds, WithinAbs (2.0, 1e-9));
    CHECK_THAT (s.getSections()[2]->seconds, WithinAbs (4.0, 1e-9));
    CHECK (Arrangement::moveSection (s, s.getSections()[0]->id, false) == nullptr);   // nothing earlier than Intro
    s.undo();
    CHECK_THAT (startOf ("chorus"), WithinAbs (4.0, 1e-9));

    // Duplicate the verse: chorus and outro move right by 2 s, verse clips are copied
    s.execute (Arrangement::duplicateSection (s, verseId));
    CHECK (s.getTracks()[0].clips.size() == 7);
    CHECK_THAT (startOf ("chorus"), WithinAbs (6.0, 1e-9));
    CHECK_THAT (startOf ("outro"), WithinAbs (8.0, 1e-9));
    int versesAt4 = 0, versesAt5 = 0;
    for (const auto& c : s.getTracks()[0].clips) { if (std::abs (c.getStartSeconds() - 4.0) < 1e-9) ++versesAt4; if (std::abs (c.getStartSeconds() - 5.0) < 1e-9) ++versesAt5; }
    CHECK (versesAt4 == 1); CHECK (versesAt5 == 1);
    REQUIRE (s.getSections().size() == 4);
    CHECK_THAT (s.getSections()[2]->seconds, WithinAbs (4.0, 1e-9));
    CHECK_THAT (s.getSections()[3]->seconds, WithinAbs (6.0, 1e-9));
    s.undo();
    CHECK (s.getTracks()[0].clips.size() == 5);
    CHECK (s.getSections().size() == 3);

    // Delete the verse's time: chorus and outro move left, verse clips vanish
    s.execute (Arrangement::deleteSectionTime (s, verseId));
    CHECK (s.getTracks()[0].clips.size() == 3);
    CHECK_THAT (startOf ("chorus"), WithinAbs (2.0, 1e-9));
    CHECK_THAT (startOf ("outro"), WithinAbs (4.0, 1e-9));
    CHECK (s.getSections().size() == 2);
    s.undo();
    CHECK (s.getTracks()[0].clips.size() == 5);
    CHECK_THAT (startOf ("chorus"), WithinAbs (4.0, 1e-9));
}

TEST_CASE ("Session files round-trip every kind of content")
{
    persistence::AudioFileLoader loader;
    persistence::LoadContext ctx;
    ctx.sampleRate = sr;
    ctx.loadAudio = [&] (const juce::File& f) -> std::shared_ptr<const juce::AudioBuffer<float>>
    {
        juce::String error;
        auto loaded = loader.load (f, sr, error);
        return loaded ? loaded->audio : nullptr;
    };
    ctx.defaultKit = [] { return engine::DrumKitFactory::createDefaultKit (sr); };

    const auto toneFile = writeTone (440.0, 1.0);
    const auto bundle = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_roundtrip.bmk");
    bundle.deleteRecursively();

    Session s;
    {
        // Audio track with an elastic clip, fades, gain line, an alternate playlist, a compressor keyed from bus 2, a send, automation
        Track audio; audio.name = "Vox"; audio.type = Track::Type::audio; audio.colour = juce::Colour (0xff112233);
        s.execute (std::make_unique<AddTrackCommand> (audio));
        AudioClip clip; clip.name = "Take"; clip.sourceFile = toneFile; clip.sampleRate = sr; clip.timelineStart = 4800; clip.sourceOffset = 100; clip.length = 40000;
        juce::String err; clip.audio = loader.load (toneFile, sr, err)->audio;
        clip.gain = 0.5f; clip.fadeIn = 480; clip.fadeOut = 960; clip.fadeOutShape = engine::FadeShape::equalPower; clip.sourceBpm = 95.0;
        auto lane = std::make_shared<engine::AutomationLane>(); lane->param = engine::ParamId::volume(); lane->points = { { 100, 0.5f }, { 2000, 1.0f } };
        clip.gainLane = lane;
        s.execute (std::make_unique<AddClipCommand> (0, clip));
        engine::StretchSpec spec; spec.mode = engine::StretchMode::rhythmic; spec.ratio = 1.25; spec.pitchSemitones = 2.0; spec.markers = { { 10000, 12000 } };
        s.execute (std::make_unique<SetClipElasticCommand> (s, ClipRef { 0, ClipRef::Kind::audio, 0 }, spec));
        Playlist alt; alt.name = "Vox.02"; AudioClip altClip = clip; altClip.name = "Alt"; altClip.timelineStart = 0;
        alt.clips.push_back (altClip);
        s.execute (std::make_unique<AddAlternatePlaylistCommand> (0, alt));
        s.execute (std::make_unique<SetInsertCommand> (0, 1, engine::EffectType::compressor, sr));
        auto p = std::make_shared<engine::InsertParams> (*s.getTracks()[0].inserts[1].params); p->values[engine::CompressorEffect::threshold] = -24.0f;
        s.execute (std::make_unique<SetInsertParamsCommand> (0, 1, p));
        s.execute (std::make_unique<SetInsertKeyCommand> (0, 1, 2, true));
        Send send; send.bus = 3; send.gain = 0.7f; send.preFader = true;
        s.execute (std::make_unique<SetSendCommand> (0, 0, send));
        auto vol = std::make_shared<engine::AutomationLane>(); vol->param = engine::ParamId::pan(); vol->points = { { 0, -1.0f }, { 48000, 1.0f } };
        s.execute (std::make_unique<ReplaceAutomationLaneCommand> (0, vol));
        s.execute (std::make_unique<SetTrackMixCommand> (0, 0.8f, -0.25f));

        // Drum machine with a pattern, MIDI instrument with a preset, aux, VCA
        Track drums; drums.name = "Drums"; drums.type = Track::Type::instrument; drums.instrumentKind = Track::InstrumentKind::drumMachine;
        drums.drumKit = engine::DrumKitFactory::createDefaultKit (sr);
        s.execute (std::make_unique<AddTrackCommand> (drums));
        PatternClip pc; pc.name = "Beat"; pc.sampleRate = sr; pc.length = 96000; pc.loopOffset = 100;
        auto pattern = std::make_shared<engine::StepPattern> (engine::StepPattern::createDefaultBeat()); pattern->set (3, 7, 77);
        pattern->setLength (0, 0, 4); pattern->setOffset (3, 7, 2);   // a held hit and a hit half a step late
        pc.pattern = pattern;
        s.execute (std::make_unique<AddPatternClipCommand> (1, pc));
        Track synth; synth.name = "Keys"; synth.type = Track::Type::instrument; synth.instrumentKind = Track::InstrumentKind::synth;
        synth.instrument = engine::Instrument::create (engine::InstrumentType::fm, sr);
        auto ip = engine::Instrument::presets (engine::InstrumentType::fm)[1];
        synth.instrumentParams = std::make_shared<const engine::InstrumentParams> (ip);
        s.execute (std::make_unique<AddTrackCommand> (synth));
        MidiClip mc; mc.name = "Arp"; mc.sampleRate = sr; mc.length = 48000; mc.sequence = std::make_shared<const engine::MidiSequence> (engine::MidiSequence::createDefaultArpeggio());
        s.execute (std::make_unique<AddMidiClipCommand> (2, mc));
        Track aux; aux.name = "Verb"; aux.type = Track::Type::aux; aux.inputBus = 3;
        s.execute (std::make_unique<AddTrackCommand> (aux));
        s.execute (std::make_unique<SetInsertCommand> (3, 0, engine::EffectType::convolution, sr));
        Track vca; vca.name = "VCA"; vca.type = Track::Type::vca;
        s.execute (std::make_unique<AddTrackCommand> (vca));
        s.execute (std::make_unique<SetTrackVcaCommand> (0, s.getTracks()[4].id));

        Group g; g.name = "Rhythm"; g.trackIds = { s.getTracks()[0].id, s.getTracks()[1].id }; g.type = Group::Type::mix;
        s.execute (std::make_unique<CreateGroupCommand> (g));
        s.execute (std::make_unique<AddMarkerCommand> (point ("Intro", 0.0, true, 8.0)));
        auto m = point ("Drop", 8.0); m.recallSelection = true; m.selectionStart = 8.0; m.selectionEnd = 12.0; m.recallZoom = true; m.pixelsPerSecond = 120.0;
        s.execute (std::make_unique<AddMarkerCommand> (m));
        RecordSettings rs; rs.mode = RecordMode::quickPunch; rs.preRoll = true; rs.preRollSeconds = 1.5;
        s.execute (std::make_unique<SetRecordSettingsCommand> (rs));
        s.execute (std::make_unique<SetInsertCommand> (-1, 0, engine::EffectType::limiter, sr));   // master insert
    }

    persistence::TransportState tsOut; tsOut.bpm = 97.5; tsOut.beatsPerBar = 3; tsOut.loopStart = 100; tsOut.loopEnd = 200000; tsOut.loopEnabled = true;
    REQUIRE (persistence::SessionFile::save (s, tsOut, bundle).isEmpty());
    CHECK (persistence::SessionFile::isSessionBundle (bundle));

    Session loaded;
    persistence::TransportState tsIn;
    juce::StringArray warnings;
    REQUIRE (persistence::SessionFile::load (loaded, tsIn, bundle, ctx, warnings).isEmpty());
    INFO (warnings.joinIntoString ("; "));
    CHECK (warnings.isEmpty());
    CHECK_FALSE (loaded.getHistory().canUndo());
    CHECK_THAT (tsIn.bpm, WithinAbs (97.5, 1e-9)); CHECK (tsIn.beatsPerBar == 3); CHECK (tsIn.loopStart == 100); CHECK (tsIn.loopEnd == 200000); CHECK (tsIn.loopEnabled);
    CHECK (loaded.getRecordSettings().mode == RecordMode::quickPunch);
    CHECK (loaded.getRecordSettings().preRoll);
    CHECK_THAT (loaded.getRecordSettings().preRollSeconds, WithinAbs (1.5, 1e-9));

    REQUIRE (loaded.getNumTracks() == 5);
    const auto& vox = loaded.getTracks()[0];
    CHECK (vox.name == "Vox"); CHECK (vox.colour == juce::Colour (0xff112233)); CHECK (vox.id == s.getTracks()[0].id);
    CHECK_THAT (vox.gain, WithinAbs (0.8f, 1e-6)); CHECK_THAT (vox.pan, WithinAbs (-0.25f, 1e-6));
    REQUIRE (vox.clips.size() == 1);
    const auto& c = vox.clips[0];
    CHECK (c.name == "Take"); CHECK (c.sourceFile == toneFile); CHECK (c.timelineStart == 4800);
    CHECK (c.isElastic()); CHECK (c.elastic.mode == engine::StretchMode::rhythmic); CHECK_THAT (c.elastic.ratio, WithinAbs (1.25, 1e-9));
    CHECK_THAT (c.elastic.pitchSemitones, WithinAbs (2.0, 1e-9)); REQUIRE (c.elastic.markers.size() == 1); CHECK (c.elastic.markers[0].output == 12000);
    CHECK (c.sourceOffset == s.getTracks()[0].clips[0].sourceOffset); CHECK (c.length == s.getTracks()[0].clips[0].length);
    CHECK (c.audio->getNumSamples() == s.getTracks()[0].clips[0].audio->getNumSamples());
    CHECK (c.sourceAudio != nullptr);
    CHECK_THAT (c.gain, WithinAbs (0.5f, 1e-6)); CHECK (c.fadeOutShape == engine::FadeShape::equalPower); CHECK_THAT (c.sourceBpm, WithinAbs (95.0, 1e-9));
    REQUIRE (c.gainLane != nullptr); CHECK (c.gainLane->points.size() == 2);
    REQUIRE (vox.alternates.size() == 1); CHECK (vox.alternates[0].name == "Vox.02"); CHECK (vox.alternates[0].clips.size() == 1);
    REQUIRE (! vox.inserts[1].isEmpty()); CHECK (vox.inserts[1].type == engine::EffectType::compressor);
    CHECK_THAT (vox.inserts[1].params->values[engine::CompressorEffect::threshold], WithinAbs (-24.0f, 1e-6));
    CHECK (vox.inserts[1].keyBus == 2); CHECK (vox.inserts[1].keyListen);
    CHECK (vox.sends[0].bus == 3); CHECK_THAT (vox.sends[0].gain, WithinAbs (0.7f, 1e-6)); CHECK (vox.sends[0].preFader);
    REQUIRE (vox.laneFor (engine::ParamId::pan()) != nullptr); CHECK (vox.laneFor (engine::ParamId::pan())->points.size() == 2);
    CHECK (vox.vcaTrackId == s.getTracks()[4].id);

    const auto& drums = loaded.getTracks()[1];
    CHECK (drums.isDrumMachine()); REQUIRE (drums.drumKit != nullptr); CHECK (drums.drumKit->pads[0].audio != nullptr);
    REQUIRE (drums.patternClips.size() == 1); CHECK (drums.patternClips[0].pattern->get (3, 7) == 77); CHECK (drums.patternClips[0].pattern->get (0, 0) == 110);
    CHECK (drums.patternClips[0].loopOffset == 100);
    CHECK (drums.patternClips[0].pattern->getLength (0, 0) == 4); CHECK (drums.patternClips[0].pattern->getLength (0, 8) == 1);
    CHECK (drums.patternClips[0].pattern->getOffset (3, 7) == 2); CHECK (drums.patternClips[0].pattern->getOffset (0, 0) == 0);

    const auto& keys = loaded.getTracks()[2];
    CHECK (keys.hasInstrument()); CHECK (keys.instrumentType() == engine::InstrumentType::fm);
    CHECK (keys.instrumentParams->presetName == engine::Instrument::presets (engine::InstrumentType::fm)[1].presetName);
    REQUIRE (keys.midiClips.size() == 1); CHECK (keys.midiClips[0].sequence->notes.size() == 16);

    CHECK (loaded.getTracks()[3].isAux()); CHECK (loaded.getTracks()[3].inputBus == 3);
    CHECK (loaded.getTracks()[3].inserts[0].type == engine::EffectType::convolution);
    CHECK (loaded.getTracks()[4].isVca());
    CHECK (loaded.getMaster().inserts[0].type == engine::EffectType::limiter);

    REQUIRE (loaded.getGroups().size() == 1); CHECK (loaded.getGroups()[0].name == "Rhythm"); CHECK (loaded.getGroups()[0].trackIds.size() == 2); CHECK (loaded.getGroups()[0].type == Group::Type::mix);
    REQUIRE (loaded.getMarkers().size() == 2);
    CHECK (loaded.getMarkers()[0].isSection); CHECK_THAT (loaded.getMarkers()[0].endSeconds, WithinAbs (8.0, 1e-9));
    CHECK (loaded.getMarkers()[1].recallSelection); CHECK_THAT (loaded.getMarkers()[1].selectionEnd, WithinAbs (12.0, 1e-9)); CHECK (loaded.getMarkers()[1].recallZoom);

    // New ids continue past the loaded ones
    Track more; more.name = "More"; more.type = Track::Type::audio;
    loaded.execute (std::make_unique<AddTrackCommand> (more));
    CHECK (loaded.getTracks()[5].id > loaded.getTracks()[4].id);

    // A missing audio file is reported and the clip skipped, but the session still opens
    toneFile.deleteFile();
    Session again; juce::StringArray w2;
    REQUIRE (persistence::SessionFile::load (again, tsIn, bundle, ctx, w2).isEmpty());
    CHECK (w2.size() >= 1);
    CHECK (again.getTracks()[0].clips.empty());
    CHECK (again.getNumTracks() == 5);

    bundle.deleteRecursively();
}

TEST_CASE ("A drum track's kit comes back by name when the loader knows the bundled kits")
{
    using namespace beatmaker;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bm-kit-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
    dir.createDirectory();
    const auto bundle = dir.getChildFile ("Kits.bmk");
    model::Session s;
    model::Track t; t.name = "Drums"; t.type = model::Track::Type::instrument; t.instrumentKind = model::Track::InstrumentKind::drumMachine;
    t.drumKit = engine::DrumKitFactory::createKit ("808", 48000.0);
    s.execute (std::make_unique<model::AddTrackCommand> (t));
    persistence::TransportState ts;
    REQUIRE (persistence::SessionFile::save (s, ts, bundle).isEmpty());

    model::Session again; persistence::TransportState ts2; juce::StringArray warnings;
    persistence::LoadContext ctx;
    ctx.defaultKit = [] { return engine::DrumKitFactory::createDefaultKit (48000.0); };
    ctx.kitNamed = [] (const juce::String& name) { return engine::DrumKitFactory::createKit (name, 48000.0); };
    REQUIRE (persistence::SessionFile::load (again, ts2, bundle, ctx, warnings).isEmpty());
    REQUIRE (again.getTracks().size() == 1);
    REQUIRE (again.getTracks()[0].drumKit != nullptr);
    CHECK (again.getTracks()[0].drumKit->name == "808");
    CHECK (again.getTracks()[0].drumKit->pads[engine::DrumKitFactory::kick].name == "808 Kick");
    // A loader without the registry still opens it, on the default kit's sounds
    model::Session older; persistence::LoadContext plain; plain.defaultKit = ctx.defaultKit;
    REQUIRE (persistence::SessionFile::load (older, ts2, bundle, plain, warnings).isEmpty());
    CHECK (older.getTracks()[0].drumKit->name == "808");
    CHECK (older.getTracks()[0].drumKit->pads[engine::DrumKitFactory::kick].name == "Kick");
    dir.deleteRecursively();
}

TEST_CASE ("Renaming a session bundle moves the folder and keeps the session whole")
{
    using namespace beatmaker;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bm-rename-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
    dir.createDirectory();
    const auto bundle = dir.getChildFile ("First.bmk");
    model::Session s;
    model::Track t; t.name = "Keys"; t.type = model::Track::Type::instrument; t.instrumentKind = model::Track::InstrumentKind::synth;
    s.execute (std::make_unique<model::AddTrackCommand> (t));
    persistence::TransportState ts; ts.bpm = 100.0;
    REQUIRE (persistence::SessionFile::save (s, ts, bundle).isEmpty());
    REQUIRE (persistence::SessionFile::isSessionBundle (bundle));

    juce::String error;
    CHECK (persistence::SessionFile::legalSessionName ("  My / Song?  ") == "My  Song");
    CHECK (persistence::SessionFile::renameBundle (bundle, "   ", error) == juce::File()); CHECK (error.isNotEmpty());
    CHECK (persistence::SessionFile::renameBundle (bundle, "First", error) == bundle);   // the same name: nothing to do
    const auto renamed = persistence::SessionFile::renameBundle (bundle, "Second", error);
    CHECK (error.isEmpty());
    CHECK (renamed == dir.getChildFile ("Second.bmk"));
    CHECK (! bundle.exists());
    CHECK (persistence::SessionFile::isSessionBundle (renamed));
    CHECK (persistence::SessionFile::sessionName (renamed) == "Second");
    // It still opens with its content
    model::Session again; persistence::TransportState ts2; juce::StringArray warnings;
    persistence::LoadContext ctx;
    CHECK (persistence::SessionFile::load (again, ts2, renamed, ctx, warnings).isEmpty());
    REQUIRE (again.getTracks().size() == 1); CHECK (again.getTracks()[0].name == "Keys"); CHECK (ts2.bpm == 100.0);
    // A name already taken is refused, the bundle untouched
    dir.getChildFile ("Third.bmk").createDirectory();
    CHECK (persistence::SessionFile::renameBundle (renamed, "Third", error) == juce::File()); CHECK (error.contains ("already"));
    CHECK (renamed.exists());
    CHECK (persistence::SessionFile::renameBundle (dir.getChildFile ("Third.bmk"), "Fourth", error) == juce::File());   // not a session
    dir.deleteRecursively();
}
