#include <catch2/catch_test_macros.hpp>
#include <SampleProjects.h>
#include <SessionFile.h>
#include <AudioFileLoader.h>
#include <dsp/DrumKitFactory.h>
#include "../ui/shared/Tutorials.h"

using namespace beatmaker;

TEST_CASE ("Every sample project builds from the bundled loops, saves and reopens cleanly")
{
    const auto loops = juce::File (__FILE__).getParentDirectory().getSiblingFile ("assets").getChildFile ("loops");
    REQUIRE (loops.isDirectory());
    persistence::AudioFileLoader loader;
    const double sr = 48000.0;
    auto loadAudio = [&] (const juce::File& f) -> std::shared_ptr<const juce::AudioBuffer<float>> { juce::String e; auto l = loader.load (f, sr, e); return l ? l->audio : nullptr; };
    persistence::LoadContext ctx; ctx.sampleRate = sr; ctx.loadAudio = loadAudio; ctx.defaultKit = [] { return engine::DrumKitFactory::createDefaultKit (48000.0); };

    const auto infos = persistence::SampleProjects::list();
    REQUIRE (infos.size() == 3);
    for (const auto& info : infos)
    {
        INFO (info.name);
        model::Session s; persistence::TransportState ts; juce::String error;
        REQUIRE (persistence::SampleProjects::create (info.name, s, ts, sr, loops, loadAudio, error));
        CHECK (error.isEmpty());
        CHECK (s.getNumTracks() >= 3);
        CHECK (ts.bpm > 0.0);
        int clips = 0; for (const auto& t : s.getTracks()) clips += (int) (t.clips.size() + t.patternClips.size() + t.midiClips.size());
        CHECK (clips >= 3);
        CHECK (! s.getMarkers().empty());
        CHECK (s.getLengthSeconds() > 5.0);
        CHECK (s.getMaster().inserts[0].type == engine::EffectType::limiter);

        const auto bundle = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("beatmaker_sample_" + juce::File::createLegalFileName (info.name) + ".bmk");
        bundle.deleteRecursively();
        REQUIRE (persistence::SessionFile::save (s, ts, bundle).isEmpty());
        model::Session again; persistence::TransportState ts2; juce::StringArray warnings;
        REQUIRE (persistence::SessionFile::load (again, ts2, bundle, ctx, warnings).isEmpty());
        CHECK (warnings.isEmpty());
        CHECK (again.getNumTracks() == s.getNumTracks());
        CHECK (again.getMarkers().size() == s.getMarkers().size());
        bundle.deleteRecursively();
    }
    model::Session s; persistence::TransportState ts; juce::String error;
    CHECK_FALSE (persistence::SampleProjects::create ("Nope", s, ts, sr, loops, loadAudio, error));
    CHECK (error.contains ("Unknown sample project"));
}

TEST_CASE ("Tutorials track their progress from step predicates")
{
    int tracks = 0; bool playing = false;
    ui::Tutorial t;
    t.title = "Make a beat";
    t.steps = { { "Add a drum track", "", "track.addDrums", [&] { return tracks > 0; } },
                { "Press play", "", "transport.playStop", [&] { return playing; } },
                { "Bounce", "", "file.bounce", nullptr } };
    CHECK (t.doneCount() == 0);
    CHECK (t.currentStep() == 0);
    tracks = 1;
    CHECK (t.doneCount() == 1);
    CHECK (t.currentStep() == 1);
    playing = true;
    CHECK (t.doneCount() == 2);
    CHECK (t.currentStep() == 2);       // a step without a predicate never completes on its own
    CHECK_FALSE (t.isComplete());
    t.steps.pop_back();
    CHECK (t.isComplete());
    CHECK (t.currentStep() == -1);
}
