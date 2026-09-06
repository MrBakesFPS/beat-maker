#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <SessionImport.h>
#include <SessionFile.h>
#include <AudioFileLoader.h>
#include <Session.h>
#include <dsp/DrumKitFactory.h>

using namespace beatmaker;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;
    persistence::LoadContext context (persistence::AudioFileLoader& loader)
    {
        persistence::LoadContext ctx;
        ctx.sampleRate = sr;
        ctx.loadAudio = [&] (const juce::File& f) -> std::shared_ptr<const juce::AudioBuffer<float>> { juce::String e; auto l = loader.load (f, sr, e); return l ? l->audio : nullptr; };
        ctx.defaultKit = [] { return engine::DrumKitFactory::createDefaultKit (sr); };
        return ctx;
    }
}

TEST_CASE ("Import Session Data brings tracks, clips, VCA links and markers from another session, offset and undoable")
{
    persistence::AudioFileLoader loader;
    const auto bundle = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_import_src.bmk");
    bundle.deleteRecursively();

    // Source session: a drum track with a pattern, a synth with MIDI, a VCA controlling the synth, two markers
    {
        Session src;
        Track drums; drums.name = "Drums"; drums.type = Track::Type::instrument; drums.instrumentKind = Track::InstrumentKind::drumMachine; drums.drumKit = engine::DrumKitFactory::createDefaultKit (sr);
        src.execute (std::make_unique<AddTrackCommand> (drums));
        PatternClip pc; pc.name = "Beat"; pc.sampleRate = sr; pc.timelineStart = 48000; pc.length = 96000; pc.pattern = std::make_shared<const engine::StepPattern> (engine::StepPattern::createDefaultBeat());
        src.execute (std::make_unique<AddPatternClipCommand> (0, pc));
        Track keys; keys.name = "Keys"; keys.type = Track::Type::instrument; keys.instrumentKind = Track::InstrumentKind::synth;
        keys.instrument = engine::Instrument::create (engine::InstrumentType::fm, sr);
        keys.instrumentParams = std::make_shared<const engine::InstrumentParams> (engine::Instrument::defaultParams (engine::InstrumentType::fm));
        src.execute (std::make_unique<AddTrackCommand> (keys));
        MidiClip mc; mc.name = "Arp"; mc.sampleRate = sr; mc.timelineStart = 0; mc.length = 48000; mc.sequence = std::make_shared<const engine::MidiSequence> (engine::MidiSequence::createDefaultArpeggio());
        src.execute (std::make_unique<AddMidiClipCommand> (1, mc));
        auto lane = std::make_shared<engine::AutomationLane>(); lane->param = engine::ParamId::volume(); lane->points = { { 0, 1.0f }, { 48000, 0.5f } };
        src.execute (std::make_unique<ReplaceAutomationLaneCommand> (1, lane));
        Track vca; vca.name = "Keys VCA"; vca.type = Track::Type::vca;
        src.execute (std::make_unique<AddTrackCommand> (vca));
        src.execute (std::make_unique<SetTrackVcaCommand> (1, src.getTracks()[2].id));
        Marker m1; m1.name = "Verse"; m1.seconds = 4.0; src.execute (std::make_unique<AddMarkerCommand> (m1));
        Marker m2; m2.name = "Chorus"; m2.seconds = 8.0; m2.endSeconds = 12.0; m2.isSection = true; src.execute (std::make_unique<AddMarkerCommand> (m2));
        persistence::TransportState ts; ts.bpm = 95.0;
        REQUIRE (persistence::SessionFile::save (src, ts, bundle).isEmpty());
    }

    // Destination already has a Drums track (for the match-by-name case) and one marker
    Session dest;
    Track existing; existing.name = "Drums"; existing.type = Track::Type::instrument; existing.instrumentKind = Track::InstrumentKind::drumMachine; existing.drumKit = engine::DrumKitFactory::createDefaultKit (sr);
    dest.execute (std::make_unique<AddTrackCommand> (existing));
    Marker dm; dm.name = "Verse"; dm.seconds = 4.0 + 2.0; dest.execute (std::make_unique<AddMarkerCommand> (dm));   // same as an offset import of Verse

    Session source; persistence::TransportState srcTransport; juce::StringArray warnings;
    REQUIRE (persistence::SessionImport::open (source, srcTransport, bundle, context (loader), warnings).isEmpty());
    REQUIRE (source.getNumTracks() == 3);
    CHECK_THAT (srcTransport.bpm, WithinAbs (95.0, 1e-9));

    // New tracks, 2 s later, with markers
    persistence::ImportOptions o;
    o.tracks = { 0, 1, 2 };
    o.offsetSeconds = 2.0;
    o.importMarkers = true;
    persistence::ImportSummary summary;
    auto cmd = persistence::SessionImport::build (dest, source, sr, o, summary);
    REQUIRE (cmd != nullptr);
    dest.execute (std::move (cmd));
    CHECK (summary.tracksAdded == 3);
    CHECK (summary.clipsAdded == 2);
    CHECK (summary.markersAdded == 1);   // "Verse" at 6 s already existed
    REQUIRE (dest.getNumTracks() == 4);
    CHECK (dest.getTracks()[1].name == "Drums");
    CHECK (dest.getTracks()[1].patternClips[0].timelineStart == 48000 + 96000);
    CHECK (dest.getTracks()[2].name == "Keys");
    CHECK (dest.getTracks()[2].midiClips[0].timelineStart == 96000);
    CHECK (dest.getTracks()[2].hasInstrument());
    CHECK (dest.getTracks()[2].vcaTrackId == dest.getTracks()[3].id);   // remapped to the imported VCA
    CHECK (dest.getTracks()[3].isVca());
    REQUIRE (dest.getTracks()[2].laneFor (engine::ParamId::volume()) != nullptr);
    CHECK (dest.getTracks()[2].laneFor (engine::ParamId::volume())->points[1].time == 48000 + 96000);
    std::set<int> ids; for (const auto& t : dest.getTracks()) ids.insert (t.id);
    CHECK (ids.size() == 4);
    CHECK (dest.getMarkers().size() == 2);
    CHECK_THAT (dest.getMarkers()[1].seconds, WithinAbs (10.0, 1e-9));
    CHECK (dest.getHistory().getUndoName() == "Import Session Data");
    dest.undo();
    CHECK (dest.getNumTracks() == 1);
    CHECK (dest.getMarkers().size() == 1);

    // Match by name: Drums merges into the existing track, Keys becomes new; VCA left out so the link drops
    persistence::ImportOptions m;
    m.tracks = { 0, 1 };
    m.destination = persistence::ImportOptions::Destination::matchByName;
    m.includeAutomation = false;
    cmd = persistence::SessionImport::build (dest, source, sr, m, summary);
    REQUIRE (cmd != nullptr);
    dest.execute (std::move (cmd));
    CHECK (summary.tracksMerged == 1);
    CHECK (summary.tracksAdded == 1);
    CHECK (dest.getNumTracks() == 2);
    CHECK (dest.getTracks()[0].patternClips.size() == 1);
    CHECK (dest.getTracks()[0].patternClips[0].timelineStart == 48000);
    CHECK (dest.getTracks()[1].name == "Keys");
    CHECK (dest.getTracks()[1].vcaTrackId == -1);
    CHECK (dest.getTracks()[1].automation.empty());

    bundle.deleteRecursively();
}
