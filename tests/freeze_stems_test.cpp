#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <Freeze.h>
#include <MixerCommands.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <bounce/Bouncer.h>
#include <dsp/Instrument.h>

using namespace beatmaker;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    struct Fixture
    {
        Session s;
        Fixture()
        {
            Track a; a.name = "Guitar"; a.type = Track::Type::audio;
            s.execute (std::make_unique<AddTrackCommand> (a));
            auto tone = std::make_shared<juce::AudioBuffer<float>> (1, 48000);
            for (int i = 0; i < 48000; ++i) tone->setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sr));
            AudioClip c; c.name = "Riff"; c.audio = tone; c.sampleRate = sr; c.length = 48000;
            s.execute (std::make_unique<AddClipCommand> (0, c));

            Track b; b.name = "Keys"; b.type = Track::Type::instrument; b.instrumentKind = Track::InstrumentKind::synth;
            b.instrument = engine::Instrument::create (engine::InstrumentType::subtractive, sr);
            auto p = engine::Instrument::defaultParams (engine::InstrumentType::subtractive);
            p.values[engine::SubtractiveParams::wave] = 3.0f; p.values[engine::SubtractiveParams::osc2] = 0.0f; p.values[engine::SubtractiveParams::attack] = 0.0f;
            p.values[engine::SubtractiveParams::sustain] = 1.0f; p.values[engine::SubtractiveParams::cutoff] = 20000.0f; p.values[engine::SubtractiveParams::filterEnv] = 0.0f;
            b.instrumentParams = std::make_shared<const engine::InstrumentParams> (p);
            s.execute (std::make_unique<AddTrackCommand> (b));
            MidiClip m; m.sampleRate = sr; m.length = 48000;
            auto seq = std::make_shared<engine::MidiSequence>(); seq->lengthBeats = 4.0; seq->notes.push_back ({ 69, 127, 0.0, 1.5 });
            m.sequence = seq;
            s.execute (std::make_unique<AddMidiClipCommand> (1, m));
            s.execute (std::make_unique<SetInsertCommand> (1, 0, engine::EffectType::saturation, sr));
            s.execute (std::make_unique<SetTrackMixCommand> (1, 0.5f, 0.6f));
            Send send; send.bus = 0; send.gain = 0.8f;
            s.execute (std::make_unique<SetSendCommand> (1, 0, send));

            Track aux; aux.name = "Verb"; aux.type = Track::Type::aux; aux.inputBus = 0;
            s.execute (std::make_unique<AddTrackCommand> (aux));
            s.execute (std::make_unique<SetInsertCommand> (-1, 0, engine::EffectType::limiter, sr));
        }
        juce::AudioBuffer<float> bounce (std::unique_ptr<engine::RenderSnapshot> snap)
        {
            engine::BounceSettings b; b.sampleRate = sr; b.bpm = 120.0; b.startSample = 0; b.endSample = 48000; b.tailSeconds = 0.0; b.trimTail = false;
            juce::AudioBuffer<float> out;
            REQUIRE (engine::Bouncer::renderToBuffer (std::move (snap), b, out).ok());
            return out;
        }
    };
}

TEST_CASE ("Freeze snapshot isolates one track pre-fader; the frozen track plays its render identically")
{
    Fixture f;
    int trim = 0;
    auto snap = Freeze::renderSnapshotForTrack (f.s, 1, trim);
    REQUIRE (snap->strips.size() == 3);
    CHECK (snap->strips[0].muted); CHECK (snap->strips[2].muted);
    CHECK_FALSE (snap->strips[1].muted);
    CHECK (snap->strips[1].gain == 1.0f); CHECK (snap->strips[1].pan == 0.0f);
    CHECK (snap->strips[1].sends.empty());
    CHECK (snap->strips[1].inserts.size() == 1);   // the saturation stays in the render
    CHECK (snap->master.inserts.empty());
    CHECK (trim == 0);

    // The freeze render: mono-summed keys through the saturation
    auto rendered = f.bounce (std::move (snap));
    CHECK (rendered.getMagnitude (0, 1000, 20000) > 0.1f);

    auto before = f.bounce (buildRenderSnapshot (f.s));
    Track::FreezeState state;
    state.audio = std::make_shared<const juce::AudioBuffer<float>> (rendered);
    state.sampleRate = sr;
    f.s.execute (std::make_unique<FreezeTrackCommand> (1, state));
    CHECK (f.s.getTracks()[1].isFrozen());
    CHECK (f.s.getHistory().getUndoName() == "Freeze Track");

    auto frozenSnap = buildRenderSnapshot (f.s);
    int keysClips = 0; for (const auto& c : frozenSnap->clips) if (c.strip == 1) ++keysClips;
    CHECK (keysClips == 1);
    CHECK (frozenSnap->instruments.empty());
    CHECK (frozenSnap->strips[1].inserts.empty());
    CHECK (frozenSnap->midiClips.empty());
    CHECK_THAT (frozenSnap->strips[1].gain, WithinAbs (0.5f, 1e-6));   // fader still live
    CHECK (frozenSnap->strips[1].sends.size() == 1);

    auto after = f.bounce (std::move (frozenSnap));
    REQUIRE (after.getNumSamples() == before.getNumSamples());
    float maxDiff = 0.0f;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < before.getNumSamples(); ++i) maxDiff = juce::jmax (maxDiff, std::abs (before.getSample (ch, i) - after.getSample (ch, i)));
    CHECK (maxDiff < 1.0e-4f);

    f.s.execute (std::make_unique<UnfreezeTrackCommand> (1));
    CHECK_FALSE (f.s.getTracks()[1].isFrozen());
    CHECK (buildRenderSnapshot (f.s)->instruments.size() == 1);
    f.s.undo();
    CHECK (f.s.getTracks()[1].isFrozen());
}

TEST_CASE ("Commit makes a new audio track holding the render and mutes the source")
{
    Fixture f;
    int trim = 0;
    auto rendered = std::make_shared<const juce::AudioBuffer<float>> (f.bounce (Freeze::renderSnapshotForTrack (f.s, 1, trim)));
    auto cmd = Freeze::commitCommand (f.s, 1, rendered, sr, juce::File ("/tmp/keys.wav"));
    REQUIRE (cmd != nullptr);
    f.s.execute (std::move (cmd));
    REQUIRE (f.s.getNumTracks() == 4);
    const auto& committed = f.s.getTracks()[2];
    CHECK (committed.isAudio());
    CHECK (committed.name == "Keys.cm");
    CHECK (committed.clips.size() == 1);
    CHECK (committed.clips[0].audio == rendered);
    CHECK_THAT (committed.gain, WithinAbs (0.5f, 1e-6));
    CHECK (committed.sends[0].bus == 0);
    CHECK (f.s.getTracks()[1].mute);
    CHECK (f.s.getTracks()[3].isAux());
    CHECK (f.s.getHistory().getUndoName() == "Commit Track");
    f.s.undo();
    CHECK (f.s.getNumTracks() == 3);
    CHECK_FALSE (f.s.getTracks()[1].mute);
}

TEST_CASE ("Stem snapshots mute everything but the stem, with optional aux returns and master inserts")
{
    Fixture f;
    CHECK (Freeze::renderableTracks (f.s) == std::vector<int> { 0, 1, 2 });
    auto stem = Freeze::stemSnapshot (f.s, 1, false, false);
    CHECK (stem->strips[0].outputMuted); CHECK_FALSE (stem->strips[0].muted);   // still feeds its sends
    CHECK_FALSE (stem->strips[1].muted); CHECK_FALSE (stem->strips[1].outputMuted);
    CHECK (stem->strips[2].muted);                                                // aux return left out by default
    CHECK_THAT (stem->strips[1].gain, WithinAbs (0.5f, 1e-6));   // post-fader
    CHECK (stem->strips[1].sends.size() == 1);
    CHECK (stem->master.inserts.empty());
    auto withAux = Freeze::stemSnapshot (f.s, 1, true, true);
    CHECK_FALSE (withAux->strips[2].muted);
    CHECK (withAux->master.inserts.size() == 1);

    // Stems sum to the mix (without master inserts): guitar + keys + aux return
    auto mix = f.bounce (Freeze::stemSnapshot (f.s, 0, true, false));   // all sources muted except guitar + aux... build the sum explicitly instead
    juce::AudioBuffer<float> sum (2, 48000); sum.clear();
    for (int t : { 0, 1, 2 })
    {
        auto part = f.bounce (Freeze::stemSnapshot (f.s, t, false, false));
        for (int ch = 0; ch < 2; ++ch) sum.addFrom (ch, 0, part, ch, 0, 48000);
    }
    auto full = buildRenderSnapshot (f.s);
    full->master.inserts.clear();
    auto reference = f.bounce (std::move (full));
    float maxDiff = 0.0f;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 48000; ++i) maxDiff = juce::jmax (maxDiff, std::abs (reference.getSample (ch, i) - sum.getSample (ch, i)));
    CHECK (maxDiff < 1.0e-4f);
    juce::ignoreUnused (mix);
}
