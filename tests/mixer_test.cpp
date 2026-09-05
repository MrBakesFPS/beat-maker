#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <MixerCommands.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <dsp/Effects.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> makeDc (int length, float value)
    {
        juce::AudioBuffer<float> b (1, length);
        juce::FloatVectorOperations::fill (b.getWritePointer (0), value, length);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }

    struct Rig
    {
        Transport t;
        AudioGraph graph { t };
        juce::AudioBuffer<float> out { 2, 64 };

        void render (std::unique_ptr<RenderSnapshot> snap, int n = 64)
        {
            graph.setSnapshot (std::move (snap));
            t.setPositionSamples (0);
            t.play();
            out.setSize (2, n, false, true, true);
            graph.renderBlock (out.getArrayOfWritePointers(), 2, n);
        }
        ~Rig() { graph.collectGarbage(); }
    };

    RenderClip dcClip (float level, int strip) { RenderClip c; c.audio = makeDc (8192, level); c.length = 8192; c.strip = strip; return c; }
}

TEST_CASE ("Strip fader scales its sources; muted strips are silent; master fader scales the mix")
{
    Rig r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back (dcClip (0.5f, 0));
    snap->clips.push_back (dcClip (0.25f, 1));
    RenderStrip a; a.gain = 0.5f;
    RenderStrip b; b.muted = true;
    snap->strips = { a, b };
    snap->master.gain = 2.0f;
    r.render (std::move (snap));
    CHECK_THAT (r.out.getSample (0, 10), WithinAbs (0.5f * 0.5f * 2.0f, 1e-6));   // only strip a, faded, then master
    CHECK_THAT (r.graph.getStripPeak (0, 0), WithinAbs (0.25f, 1e-6));
    CHECK_THAT (r.graph.getStripPeak (1, 0), WithinAbs (0.0f, 1e-6));
    CHECK_THAT (r.graph.getMasterPeak (0), WithinAbs (0.5f, 1e-6));
}

TEST_CASE ("Sends feed buses, aux strips read them; pre-fader sends ignore the fader; track output can go to a bus")
{
    Rig r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back (dcClip (1.0f, 0));

    RenderStrip src; src.gain = 0.5f;
    src.sends.push_back ({ 0, 0.5f, /*pre*/ true });    // Bus 1: 1.0 * 0.5 (pre-fader)
    src.sends.push_back ({ 1, 1.0f, /*pre*/ false });   // Bus 2: 1.0 * 0.5 (post-fader)
    src.outputBus = 2;                                   // main gets nothing directly

    RenderStrip auxA; auxA.isAux = true; auxA.inputBus = 0; auxA.gain = 1.0f;
    RenderStrip auxB; auxB.isAux = true; auxB.inputBus = 1; auxB.gain = 1.0f; auxB.muted = true;
    RenderStrip auxC; auxC.isAux = true; auxC.inputBus = 2; auxC.gain = 2.0f;
    snap->strips = { src, auxA, auxB, auxC };
    r.render (std::move (snap));

    // main = auxA (0.5) + auxB (muted) + auxC (0.5 * 2.0)
    CHECK_THAT (r.out.getSample (0, 10), WithinAbs (0.5f + 1.0f, 1e-6));
    CHECK_THAT (r.graph.getStripPeak (1, 0), WithinAbs (0.5f, 1e-6));
    CHECK_THAT (r.graph.getStripPeak (2, 0), WithinAbs (0.0f, 1e-6));
    CHECK_THAT (r.graph.getStripPeak (3, 0), WithinAbs (1.0f, 1e-6));
}

TEST_CASE ("Without strips the graph behaves as a single pass-through strip")
{
    Rig r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back (dcClip (0.3f, 0));
    r.render (std::move (snap));
    CHECK_THAT (r.out.getSample (1, 5), WithinAbs (0.3f, 1e-6));
}

TEST_CASE ("Inserts process the strip and bypass skips them; the same code runs on the master")
{
    // A -18 dB "EQ" is hard to check exactly; use the compressor with makeup as a plain gain stage:
    // threshold 0 dB with silence below -> only makeup applies.
    auto params = std::make_shared<const InsertParams> ([] { auto p = Effect::defaultParams (EffectType::compressor);
        p.values[CompressorEffect::threshold] = 0.0f; p.values[CompressorEffect::makeup] = 6.0206f; return p; }());
    auto fx = std::shared_ptr<Effect> (Effect::create (EffectType::compressor, 44100.0));
    auto fxMaster = std::shared_ptr<Effect> (Effect::create (EffectType::compressor, 44100.0));

    Rig r;
    auto snap = std::make_unique<RenderSnapshot>();
    snap->clips.push_back (dcClip (0.1f, 0));
    RenderStrip s; s.inserts.push_back ({ fx, params, false });
    snap->strips = { s };
    snap->master.inserts.push_back ({ fxMaster, params, false });
    r.render (std::move (snap), 2048);
    CHECK_THAT (r.out.getSample (0, 2000), WithinAbs (0.4f, 1e-3));   // +6 dB twice

    auto snap2 = std::make_unique<RenderSnapshot>();
    snap2->clips.push_back (dcClip (0.1f, 0));
    RenderStrip s2; s2.inserts.push_back ({ fx, params, /*bypass*/ true });
    snap2->strips = { s2 };
    r.render (std::move (snap2), 256);
    CHECK_THAT (r.out.getSample (0, 200), WithinAbs (0.1f, 1e-6));
}

TEST_CASE ("Effects: EQ is transparent when flat and boosts a shelf; compressor reduces loud signals; delay echoes; reverb has a tail")
{
    const double sr = 48000.0;

    // EQ flat = identity
    {
        auto eq = Effect::create (EffectType::eq, sr);
        auto p = Effect::defaultParams (EffectType::eq);
        juce::AudioBuffer<float> b (2, 256);
        for (int i = 0; i < 256; ++i) { b.setSample (0, i, std::sin (i * 0.1f)); b.setSample (1, i, std::sin (i * 0.1f)); }
        auto copy = b;
        eq->process (b, 256, p);
        for (int i = 0; i < 256; ++i) CHECK (b.getSample (0, i) == copy.getSample (0, i));

        // Low shelf +12 dB on a 60 Hz sine roughly quadruples it after settling
        p.values[EqEffect::lowGain] = 12.0f; p.values[EqEffect::lowFreq] = 300.0f;
        juce::AudioBuffer<float> lo (2, 48000);
        for (int i = 0; i < 48000; ++i) lo.setSample (0, i, 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 60.0 * i / sr));
        eq->reset();
        eq->process (lo, 48000, p);
        CHECK_THAT (lo.getMagnitude (0, 24000, 24000), WithinAbs (0.1f * 3.98f, 0.03f));
    }

    // Compressor: loud steady signal is reduced toward the threshold
    {
        auto comp = Effect::create (EffectType::compressor, sr);
        auto p = Effect::defaultParams (EffectType::compressor);
        p.values[CompressorEffect::threshold] = -20.0f; p.values[CompressorEffect::ratio] = 4.0f;
        p.values[CompressorEffect::attack] = 1.0f; p.values[CompressorEffect::release] = 50.0f;
        juce::AudioBuffer<float> b (2, 48000);
        juce::FloatVectorOperations::fill (b.getWritePointer (0), 1.0f, 48000);   // 0 dBFS DC
        juce::FloatVectorOperations::fill (b.getWritePointer (1), 1.0f, 48000);
        comp->process (b, 48000, p);
        // 20 dB over threshold at 4:1 -> 15 dB reduction -> ~0.178
        CHECK_THAT (b.getSample (0, 47000), WithinAbs (0.178f, 0.01f));
        CHECK_THAT (comp->getMeter(), WithinAbs (15.0f, 0.5f));
    }

    // Delay: an impulse comes back after `time` ms at mix level
    {
        auto delay = Effect::create (EffectType::delay, sr);
        auto p = Effect::defaultParams (EffectType::delay);
        p.values[DelayEffect::time] = 100.0f; p.values[DelayEffect::feedback] = 0.0f; p.values[DelayEffect::mix] = 50.0f;
        juce::AudioBuffer<float> b (2, 8000);
        b.clear();
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        delay->process (b, 8000, p);
        CHECK_THAT (b.getSample (0, 0), WithinAbs (0.5f, 1e-6));       // dry
        CHECK_THAT (b.getSample (0, 4800), WithinAbs (0.5f, 1e-6));    // echo at 100 ms
        CHECK (b.getMagnitude (0, 1, 4799) == 0.0f);
    }

    // Reverb: an impulse produces a decaying tail
    {
        auto rev = Effect::create (EffectType::reverb, sr);
        auto p = Effect::defaultParams (EffectType::reverb);
        juce::AudioBuffer<float> b (2, 48000);
        b.clear();
        b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f);
        rev->process (b, 48000, p);
        CHECK (b.getMagnitude (0, 2000, 10000) > 0.001f);
        CHECK (b.getMagnitude (0, 40000, 8000) < b.getMagnitude (0, 2000, 10000));
    }
}

TEST_CASE ("Mixer commands: inserts keep their instance across undo/redo, sends and routing clamp and undo")
{
    using namespace beatmaker::model;
    Session s;
    Track t; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));

    s.execute (std::make_unique<SetInsertCommand> (0, 2, EffectType::reverb, 48000.0));
    const auto& ins = s.getTracks()[0].inserts[2];
    REQUIRE_FALSE (ins.isEmpty());
    CHECK (ins.type == EffectType::reverb);
    CHECK (ins.instance->getSampleRate() == 48000.0);
    auto* instance = ins.instance.get();

    s.undo();
    CHECK (s.getTracks()[0].inserts[2].isEmpty());
    s.redo();
    CHECK (s.getTracks()[0].inserts[2].instance.get() == instance);      // same instance

    auto p = std::make_shared<const InsertParams> ([] { auto d = Effect::defaultParams (EffectType::reverb); d.values[ReverbEffect::mix] = 80.0f; return d; }());
    s.execute (std::make_unique<SetInsertParamsCommand> (0, 2, p));
    CHECK (s.getTracks()[0].inserts[2].params == p);
    s.execute (std::make_unique<SetInsertBypassCommand> (0, 2, true));
    CHECK (s.getTracks()[0].inserts[2].bypass);

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->strips.size() == 1);
    REQUIRE (snap->strips[0].inserts.size() == 1);
    CHECK (snap->strips[0].inserts[0].fx.get() == instance);
    CHECK (snap->strips[0].inserts[0].bypass);

    // Replacing an insert and undoing restores the old instance
    s.execute (std::make_unique<SetInsertCommand> (0, 2, EffectType::delay, 48000.0));
    CHECK (s.getTracks()[0].inserts[2].type == EffectType::delay);
    s.undo();
    CHECK (s.getTracks()[0].inserts[2].instance.get() == instance);

    // Master inserts via index -1
    s.execute (std::make_unique<SetInsertCommand> (-1, 0, EffectType::eq, 48000.0));
    CHECK (s.getMaster().inserts[0].type == EffectType::eq);
    CHECK (buildRenderSnapshot (s)->master.inserts.size() == 1);

    // Sends
    s.execute (std::make_unique<SetSendCommand> (0, 1, Send { 3, 5.0f, true }));
    CHECK (s.getTracks()[0].sends[1].bus == 3);
    CHECK (s.getTracks()[0].sends[1].gain == 2.0f);   // clamped
    CHECK (s.getTracks()[0].sends[1].preFader);
    snap = buildRenderSnapshot (s);
    REQUIRE (snap->strips[0].sends.size() == 1);
    CHECK (snap->strips[0].sends[0].bus == 3);
    s.undo();
    CHECK_FALSE (s.getTracks()[0].sends[1].isActive());

    // Routing
    s.execute (std::make_unique<SetTrackRoutingCommand> (0, -1, 99));
    CHECK (s.getTracks()[0].outputBus == -1);         // out of range -> main
    s.execute (std::make_unique<SetTrackRoutingCommand> (0, -1, 4));
    CHECK (buildRenderSnapshot (s)->strips[0].outputBus == 4);
    s.undo();
    CHECK (s.getTracks()[0].outputBus == -1);

    // Aux track shows up as an aux strip
    Track aux; aux.type = Track::Type::aux; aux.inputBus = 4;
    s.execute (std::make_unique<AddTrackCommand> (aux));
    snap = buildRenderSnapshot (s);
    REQUIRE (snap->strips.size() == 2);
    CHECK (snap->strips[1].isAux);
    CHECK (snap->strips[1].inputBus == 4);
}
