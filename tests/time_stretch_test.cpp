#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <dsp/TimeStretch.h>
#include <dsp/Transients.h>
#include <Elastic.h>
#include <Session.h>
#include <thread>

using namespace beatmaker::engine;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    juce::AudioBuffer<float> sine (double hz, double seconds, int channels = 1)
    {
        juce::AudioBuffer<float> b (channels, (int) (seconds * sr));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sr));
        return b;
    }

    double zeroCrossingHz (const juce::AudioBuffer<float>& buf, int from, int to)
    {
        const float* d = buf.getReadPointer (0);
        int crossings = 0;
        for (int i = from + 1; i < to; ++i) if ((d[i - 1] < 0.0f) != (d[i] < 0.0f)) ++crossings;
        return crossings * 0.5 * sr / (to - from);
    }

    // Silence with short decaying bursts at the given sample positions.
    juce::AudioBuffer<float> clicks (const std::vector<int>& positions, double seconds)
    {
        juce::AudioBuffer<float> b (1, (int) (seconds * sr));
        b.clear();
        for (int p : positions)
            for (int i = 0; i < 2400 && p + i < b.getNumSamples(); ++i)
                b.setSample (0, p + i, 0.8f * (float) std::exp (-i / 400.0) * (float) std::sin (0.3 * i));
        return b;
    }

    int firstLoudSample (const juce::AudioBuffer<float>& b, int from, float threshold = 0.1f)
    {
        for (int i = from; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > threshold) return i;
        return -1;
    }
}

TEST_CASE ("Stretch ratio sets the output length exactly and keeps pitch in the Rubber Band modes")
{
    const auto src = sine (440.0, 1.0);
    for (auto mode : { StretchMode::polyphonic, StretchMode::rhythmic, StretchMode::monophonic })
    {
        INFO (TimeStretch::modeName (mode));
        StretchSpec spec; spec.mode = mode; spec.ratio = 1.5;
        auto out = TimeStretch::render (src, sr, spec);
        REQUIRE (out.getNumSamples() == 72000);
        CHECK (out.getNumSamples() == TimeStretch::outputLength (48000, spec));
        CHECK_THAT (zeroCrossingHz (out, 12000, 60000), WithinAbs (440.0, 4.0));
        CHECK (out.getMagnitude (0, 12000, 48000) > 0.3f);
    }
}

TEST_CASE ("Varispeed changes pitch with speed; pitch shift changes pitch without length")
{
    const auto src = sine (440.0, 1.0);
    StretchSpec vari; vari.mode = StretchMode::varispeed; vari.ratio = 0.5;   // twice as fast
    auto fast = TimeStretch::render (src, sr, vari);
    REQUIRE (fast.getNumSamples() == 24000);
    CHECK_THAT (zeroCrossingHz (fast, 2000, 22000), WithinAbs (880.0, 6.0));

    StretchSpec shift; shift.mode = StretchMode::polyphonic; shift.ratio = 1.0; shift.pitchSemitones = 12.0;
    auto up = TimeStretch::render (src, sr, shift);
    REQUIRE (up.getNumSamples() == 48000);
    CHECK_THAT (zeroCrossingHz (up, 8000, 40000), WithinAbs (880.0, 8.0));

    StretchSpec off;
    auto same = TimeStretch::render (src, sr, off);
    CHECK (same.getNumSamples() == 48000);
    CHECK (same.getSample (0, 1000) == src.getSample (0, 1000));
}

TEST_CASE ("Warp markers pin source positions to output positions")
{
    // Clicks at 0.25 s and 0.75 s; pin the second one to 1.0 s in a 2x stretch (it would land at 1.5 s).
    const auto src = clicks ({ 12000, 36000 }, 1.0);
    StretchSpec spec; spec.mode = StretchMode::rhythmic; spec.ratio = 2.0;
    spec.markers.push_back ({ 36000, 48000 });

    CHECK (TimeStretch::sourceToOutput (36000, 48000, spec) == 48000);
    CHECK (TimeStretch::sourceToOutput (18000, 48000, spec) == 24000);   // 4/3 ratio before the marker
    CHECK (TimeStretch::sourceToOutput (42000, 48000, spec) == 72000);   // 4x after it
    CHECK (TimeStretch::outputToSource (48000, 48000, spec) == 36000);
    CHECK (TimeStretch::outputToSource (72000, 48000, spec) == 42000);
    CHECK (TimeStretch::outputToSource (TimeStretch::sourceToOutput (5000, 48000, spec), 48000, spec) == 5000);

    for (auto mode : { StretchMode::rhythmic, StretchMode::varispeed })
    {
        INFO (TimeStretch::modeName (mode));
        spec.mode = mode;
        auto out = TimeStretch::render (src, sr, spec);
        REQUIRE (out.getNumSamples() == 96000);
        const int first = firstLoudSample (out, 0);
        const int second = firstLoudSample (out, 40000);
        CHECK_THAT ((double) first, WithinAbs (16000.0, 1500.0));    // 12000 * 4/3
        CHECK_THAT ((double) second, WithinAbs (48000.0, 1500.0));   // pinned
    }
}

TEST_CASE ("Transient detector finds clicks and ignores steady tones")
{
    const auto src = clicks ({ 4800, 24000, 40000 }, 1.0);
    const auto hits = Transients::detect (src, sr);
    REQUIRE (hits.size() == 3);
    CHECK_THAT ((double) hits[0], WithinAbs (4800.0, 300.0));
    CHECK_THAT ((double) hits[1], WithinAbs (24000.0, 300.0));
    CHECK_THAT ((double) hits[2], WithinAbs (40000.0, 300.0));

    CHECK (Transients::detect (sine (220.0, 1.0), sr).empty());
    juce::AudioBuffer<float> silence (1, 48000); silence.clear();
    CHECK (Transients::detect (silence, sr).empty());
}

namespace
{
    struct Fixture
    {
        Session session;
        Fixture (const juce::AudioBuffer<float>& audio)
        {
            Track t; t.name = "Audio"; t.type = Track::Type::audio;
            session.execute (std::make_unique<AddTrackCommand> (t));
            AudioClip clip;
            clip.name = "Clip";
            clip.audio = std::make_shared<const juce::AudioBuffer<float>> (audio);
            clip.sampleRate = sr;
            clip.timelineStart = 48000;
            clip.length = audio.getNumSamples();
            clip.fadeIn = 480;
            session.execute (std::make_unique<AddClipCommand> (0, clip));
        }
        const AudioClip& clip() const { return session.getTracks()[0].clips[0]; }
        ClipRef ref() const { return { 0, ClipRef::Kind::audio, 0 }; }
    };
}

TEST_CASE ("SetClipElasticCommand re-renders, remaps the visible region and fades, and undoes")
{
    Fixture f (sine (440.0, 1.0));
    const auto original = f.clip().audio;

    // TCE: make the 1 s clip 1.5 s long
    auto spec = Elastic::forVisibleLength (f.clip(), 72000);
    CHECK (spec.mode == StretchMode::polyphonic);
    CHECK_THAT (spec.ratio, WithinAbs (1.5, 1e-9));
    f.session.execute (std::make_unique<SetClipElasticCommand> (f.session, f.ref(), spec, "TCE Trim"));

    CHECK (f.clip().isElastic());
    CHECK (f.clip().length == 72000);
    CHECK (f.clip().timelineStart == 48000);
    CHECK (f.clip().sourceOffset == 0);
    CHECK (f.clip().fadeIn == 720);
    CHECK (f.clip().sourceAudio == original);
    CHECK (f.clip().audio->getNumSamples() == 72000);
    CHECK (f.session.getHistory().getUndoName() == "TCE Trim");

    // Trim the visible region to the middle half, then change the mode: the same source material stays visible.
    f.session.execute (std::make_unique<TrimClipCommand> (f.ref(), 48000 + 18000, 36000));
    CHECK (f.clip().sourceOffset == 18000);
    f.session.execute (std::make_unique<SetClipElasticCommand> (f.session, f.ref(), Elastic::withMode (f.clip(), StretchMode::rhythmic)));
    CHECK (f.clip().elastic.mode == StretchMode::rhythmic);
    CHECK (f.clip().sourceOffset == 18000);
    CHECK (f.clip().length == 36000);

    // Elastic off restores the original audio and maps the region back to source samples.
    f.session.execute (std::make_unique<SetClipElasticCommand> (f.session, f.ref(), Elastic::withMode (f.clip(), StretchMode::off)));
    CHECK_FALSE (f.clip().isElastic());
    CHECK (f.clip().audio == original);
    CHECK (f.clip().sourceAudio == nullptr);
    CHECK (f.clip().sourceOffset == 12000);
    CHECK (f.clip().length == 24000);

    f.session.undo(); f.session.undo(); f.session.undo(); f.session.undo();
    CHECK_FALSE (f.clip().isElastic());
    CHECK (f.clip().audio == original);
    CHECK (f.clip().length == 48000);
    CHECK (f.clip().fadeIn == 480);
}

TEST_CASE ("Conform to tempo uses the clip's source tempo and keeps the timeline start")
{
    Fixture f (sine (220.0, 2.0));
    AudioClip withBpm = f.clip();
    // Give the clip a source tempo via a fresh command path: rebuild through the session
    auto& tracks = EditAccess::tracks (f.session);
    tracks[0].clips[0].sourceBpm = 100.0;

    auto spec = Elastic::forTempo (f.clip(), 120.0, StretchMode::rhythmic);
    CHECK (spec.mode == StretchMode::rhythmic);
    CHECK_THAT (spec.ratio, WithinAbs (100.0 / 120.0, 1e-9));
    f.session.execute (std::make_unique<SetClipElasticCommand> (f.session, f.ref(), spec, "Conform to Tempo"));
    CHECK (f.clip().length == 80000);
    CHECK (f.clip().timelineStart == 48000);
    CHECK_THAT (zeroCrossingHz (*f.clip().audio, 10000, 70000), WithinAbs (220.0, 4.0));
    juce::ignoreUnused (withBpm);
}

TEST_CASE ("Quantize to grid warps transients onto grid lines")
{
    // Clip at bar 1 (timeline 48000). At 120 BPM a beat is 24000 samples.
    // Hits at beats 0, 1.1, 2.0, 2.85 relative to the clip start.
    Fixture f (clicks ({ 0, 26400, 48000, 68400 }, 2.0));
    auto spec = Elastic::quantizeToGrid (f.clip(), 120.0, 1.0);
    REQUIRE (spec.has_value());
    CHECK (spec->mode == StretchMode::rhythmic);
    // The first hit sits at the clip start (no marker at 0); every other hit gets a marker,
    // including the one already on the grid, so its neighbours' warps can't move it.
    REQUIRE (spec->markers.size() == 3);
    CHECK_THAT ((double) spec->markers[0].output, WithinAbs (24000.0, 300.0));
    CHECK_THAT ((double) spec->markers[1].output, WithinAbs (48000.0, 300.0));
    CHECK_THAT ((double) spec->markers[2].output, WithinAbs (72000.0, 300.0));

    f.session.execute (std::make_unique<SetClipElasticCommand> (f.session, f.ref(), *spec, "Quantize Audio"));
    // (A hit at the very first sample has no "before" to rise from, so the detector reports the other three.)
    const auto hits = Elastic::transients (f.clip());
    REQUIRE (hits.size() == 3);
    CHECK_THAT ((double) hits[0], WithinAbs (24000.0, 600.0));
    CHECK_THAT ((double) hits[1], WithinAbs (48000.0, 600.0));
    CHECK_THAT ((double) hits[2], WithinAbs (72000.0, 600.0));

    // Half strength moves them halfway.
    f.session.undo();
    auto half = Elastic::quantizeToGrid (f.clip(), 120.0, 1.0, 0.5f);
    REQUIRE (half.has_value());
    CHECK_THAT ((double) half->markers[0].output, WithinAbs (25200.0, 300.0));
}

TEST_CASE ("Separate at transients and tab-to-transient")
{
    Fixture f (clicks ({ 0, 24000, 48000 }, 2.0));
    auto next = Elastic::nextTransient (f.session, -1, 48000 + 100, true);
    REQUIRE (next.has_value());
    CHECK_THAT ((double) *next, WithinAbs (72000.0, 300.0));
    auto prev = Elastic::nextTransient (f.session, 0, 48000 + 60000, false);
    REQUIRE (prev.has_value());
    CHECK_THAT ((double) *prev, WithinAbs (96000.0, 300.0));
    CHECK_FALSE (Elastic::nextTransient (f.session, -1, 48000 + 96000 + 1000, true).has_value());

    auto cmd = Elastic::separateAtTransients (f.session, f.ref());
    REQUIRE (cmd != nullptr);
    f.session.execute (std::move (cmd));
    CHECK (f.session.getTracks()[0].clips.size() == 3);   // hit at the clip start doesn't split
    CHECK (f.session.getHistory().getUndoName() == "Separate at Transients");
    f.session.undo();
    CHECK (f.session.getTracks()[0].clips.size() == 1);
}

TEST_CASE ("Warp marker helpers add, move within neighbours, and remove")
{
    Fixture f (sine (440.0, 1.0));
    auto spec = Elastic::withMarkerAt (f.clip(), 12000);
    CHECK (spec.mode == StretchMode::polyphonic);
    REQUIRE (spec.markers.size() == 1);
    CHECK (spec.markers[0].source == 12000);
    f.session.execute (std::make_unique<SetClipElasticCommand> (f.session, f.ref(), spec));

    spec = Elastic::withMarkerAt (f.clip(), 36000);
    f.session.execute (std::make_unique<SetClipElasticCommand> (f.session, f.ref(), spec));
    REQUIRE (f.clip().elastic.markers.size() == 2);

    spec = Elastic::withMarkerMoved (f.clip(), 0, 40000);   // can't pass the second marker
    CHECK (spec.markers[0].output == 36000 - 1);
    spec = Elastic::withMarkerMoved (f.clip(), 0, 18000);
    CHECK (spec.markers[0].output == 18000);
    f.session.execute (std::make_unique<SetClipElasticCommand> (f.session, f.ref(), spec));
    CHECK (f.clip().length == 48000);   // markers alone don't change the length
    CHECK (TimeStretch::sourceToOutput (12000, 48000, f.clip().elastic) == 18000);

    spec = Elastic::withoutMarker (f.clip(), 1);
    CHECK (spec.markers.size() == 1);
    spec = Elastic::withoutMarkers (f.clip());
    CHECK (spec.markers.empty());
}

TEST_CASE ("Elastic commands can render on a background thread and refuse to land on a changed clip")
{
    Fixture f (sine (440.0, 1.0));
    auto spec = Elastic::forVisibleLength (f.clip(), 72000);

    // Deferred: capture now, render elsewhere, apply later
    auto deferred = std::make_unique<SetClipElasticCommand> (f.session, f.ref(), spec, "TCE Trim", true);
    CHECK_FALSE (deferred->isRendered());
    CHECK (deferred->getSourceLength() == 48000);
    std::thread worker ([&] { deferred->render(); });
    worker.join();
    CHECK (deferred->isRendered());

    // Same result as the synchronous form
    SetClipElasticCommand sync (f.session, f.ref(), spec, "TCE Trim");
    f.session.execute (std::move (deferred));
    CHECK (f.clip().length == 72000);
    CHECK (f.clip().audio->getNumSamples() == 72000);
    f.session.undo();
    CHECK (f.clip().length == 48000);

    // Cancelled render: nothing happens
    auto cancelled = std::make_unique<SetClipElasticCommand> (f.session, f.ref(), spec, "TCE Trim", true);
    CHECK_FALSE (cancelled->render ([] (double) { return false; }));
    CHECK_FALSE (cancelled->isRendered());
    f.session.execute (std::move (cancelled));
    CHECK (f.clip().length == 48000);

    // The clip changed while rendering: the command is stale and leaves it alone
    auto stale = std::make_unique<SetClipElasticCommand> (f.session, f.ref(), spec, "TCE Trim", true);
    stale->render();
    f.session.execute (std::make_unique<TrimClipCommand> (f.ref(), 48000, 24000));
    auto* raw = stale.get();
    f.session.execute (std::move (stale));
    CHECK (raw->wasStale());
    CHECK (f.clip().length == 24000);
    CHECK_FALSE (f.clip().isElastic());
}
