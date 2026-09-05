#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <DelayCompensation.h>
#include <MixerCommands.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <dsp/Effects.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> impulse (int length, int at)
    {
        juce::AudioBuffer<float> b (1, length);
        b.clear();
        b.setSample (0, at, 1.0f);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }

    std::vector<int> nonZero (const juce::AudioBuffer<float>& out, int ch)
    {
        std::vector<int> v;
        for (int i = 0; i < out.getNumSamples(); ++i) if (out.getSample (ch, i) != 0.0f) v.push_back (i);
        return v;
    }
}

TEST_CASE ("Compressor lookahead reports and produces real latency")
{
    auto comp = Effect::create (EffectType::compressor, 48000.0);
    auto p = Effect::defaultParams (EffectType::compressor);
    CHECK (comp->getLatencySamples (p) == 0);
    p.values[CompressorEffect::lookahead] = 5.0f;        // 5 ms = 240 samples
    CHECK (comp->getLatencySamples (p) == 240);

    p.values[CompressorEffect::threshold] = 0.0f;         // no compression of a quiet impulse: pure delay
    juce::AudioBuffer<float> b (2, 1000);
    b.clear();
    b.setSample (0, 10, 0.1f); b.setSample (1, 10, 0.1f);
    comp->process (b, 1000, p);
    CHECK (nonZero (b, 0) == std::vector<int> { 250 });
    CHECK_THAT (b.getSample (0, 250), WithinAbs (0.1f, 1e-4));
}

TEST_CASE ("Delay compensation aligns source strips, buses and aux returns")
{
    Session s;
    Track a; a.type = Track::Type::audio;            // 240 samples of latency, to main
    Track b; b.type = Track::Type::audio;            // no inserts, to main
    Track c; c.type = Track::Type::audio;            // no inserts, routed to bus 0
    Track aux; aux.type = Track::Type::aux; aux.inputBus = 0;   // 100 samples of latency
    for (auto& t : { a, b, c, aux }) s.execute (std::make_unique<AddTrackCommand> (t));
    s.execute (std::make_unique<SetTrackRoutingCommand> (2, -1, 0));

    s.execute (std::make_unique<SetInsertCommand> (0, 0, EffectType::compressor, 48000.0));
    auto pa = std::make_shared<const InsertParams> ([] { auto d = Effect::defaultParams (EffectType::compressor); d.values[CompressorEffect::lookahead] = 5.0f; return d; }());
    s.execute (std::make_unique<SetInsertParamsCommand> (0, 0, pa));
    s.execute (std::make_unique<SetInsertCommand> (3, 0, EffectType::compressor, 48000.0));
    auto px = std::make_shared<const InsertParams> ([] { auto d = Effect::defaultParams (EffectType::compressor); d.values[CompressorEffect::lookahead] = 100.0f / 48.0f; return d; }());
    s.execute (std::make_unique<SetInsertParamsCommand> (3, 0, px));

    auto d = DelayCompensation::compute (s);
    REQUIRE (d.size() == 4);
    CHECK (d[0].insertLatency == 240);
    CHECK (d[3].insertLatency == 100);
    // Sources align at 240; anything straight to main also waits for the aux stage (100)
    CHECK (d[0].compensation == 0 + 100);        // 240 - 240 + 100
    CHECK (d[1].compensation == 240 + 100);
    CHECK (d[2].compensation == 240);            // feeds the bus: align at the bus only
    CHECK (d[3].compensation == 0);              // the slowest aux
    // Every path to main totals 340 samples of latency
    CHECK (d[0].insertLatency + d[0].total() == 340);
    CHECK (d[1].insertLatency + d[1].total() == 340);
    CHECK (d[2].insertLatency + d[2].total() + d[3].insertLatency + d[3].total() == 340);

    // User offsets add; negative offsets can't make the total negative
    s.execute (std::make_unique<SetTrackDelayOffsetCommand> (1, -50));
    d = DelayCompensation::compute (s);
    CHECK (d[1].userOffset == -50);
    CHECK (d[1].total() == 290);
    s.execute (std::make_unique<SetTrackDelayOffsetCommand> (3, -500));
    CHECK (DelayCompensation::compute (s)[3].total() == 0);
    s.undo(); s.undo();

    // Off: no compensation, latencies still reported
    s.execute (std::make_unique<SetDelayCompensationCommand> (false));
    d = DelayCompensation::compute (s);
    CHECK (d[1].compensation == 0);
    CHECK (d[0].insertLatency == 240);
    s.undo();
    CHECK (DelayCompensation::compute (s)[1].compensation == 340);

    auto snap = buildRenderSnapshot (s);
    CHECK (snap->strips[1].delaySamples == 340);
    CHECK (snap->strips[2].delaySamples == 240);
}

TEST_CASE ("Graph applies strip delays so simultaneous impulses arrive together")
{
    Transport t;
    t.setSampleRate (48000.0);
    AudioGraph graph (t);

    auto comp = std::shared_ptr<Effect> (Effect::create (EffectType::compressor, 48000.0));
    auto params = std::make_shared<const InsertParams> ([] { auto d = Effect::defaultParams (EffectType::compressor);
        d.values[CompressorEffect::threshold] = 0.0f; d.values[CompressorEffect::lookahead] = 5.0f; return d; }());

    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip c1; c1.audio = impulse (4000, 100); c1.length = 4000; c1.strip = 0;
    RenderClip c2; c2.audio = impulse (4000, 100); c2.length = 4000; c2.strip = 1;
    snap->clips = { c1, c2 };
    RenderStrip slow; slow.inserts.push_back ({ comp, params, false, 0 });       // 240 samples late
    RenderStrip fast; fast.delaySamples = 240;                                    // compensated
    snap->strips = { slow, fast };
    graph.setSnapshot (std::move (snap));
    t.play();

    juce::AudioBuffer<float> out (2, 2048);
    for (int pos = 0; pos < 2048; pos += 256)
    {
        float* ptrs[2] = { out.getWritePointer (0, pos), out.getWritePointer (1, pos) };
        graph.renderBlock (ptrs, 2, 256);
    }
    const auto hits = nonZero (out, 0);
    REQUIRE (hits.size() == 1);                     // both impulses land on the same sample
    CHECK (hits[0] == 340);
    CHECK_THAT (out.getSample (0, 340), WithinAbs (2.0f, 1e-3));   // 1.0 + 1.0 (each summed to both channels at unity)
    graph.collectGarbage();
}

TEST_CASE ("Output paths: direct outs bypass the master; the main path can sit on other channels")
{
    Transport t;
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip c1; c1.audio = impulse (100, 5); c1.length = 100; c1.strip = 0;
    RenderClip c2; c2.audio = impulse (100, 5); c2.length = 100; c2.strip = 1;
    snap->clips = { c1, c2 };
    RenderStrip main; RenderStrip direct; direct.outputChannel = 2;
    snap->strips = { main, direct };
    snap->master.gain = 0.5f;
    snap->mainOutputChannel = 0;
    graph.setSnapshot (std::move (snap));
    t.play();

    juce::AudioBuffer<float> out (4, 64);
    graph.renderBlock (out.getArrayOfWritePointers(), 4, 64);
    CHECK_THAT (out.getSample (0, 5), WithinAbs (0.5f, 1e-6));   // through the master
    CHECK_THAT (out.getSample (2, 5), WithinAbs (1.0f, 1e-6));   // direct out, no master gain
    CHECK_THAT (out.getSample (3, 5), WithinAbs (1.0f, 1e-6));

    auto snap2 = std::make_unique<RenderSnapshot>();
    snap2->clips = { c1 };
    snap2->strips = { main };
    snap2->mainOutputChannel = 2;
    graph.setSnapshot (std::move (snap2));
    t.setPositionSamples (0);
    out.clear();
    graph.renderBlock (out.getArrayOfWritePointers(), 4, 64);
    CHECK (out.getSample (0, 5) == 0.0f);
    CHECK_THAT (out.getSample (2, 5), WithinAbs (1.0f, 1e-6));
    graph.collectGarbage();
}

TEST_CASE ("I/O Setup: defaults, commands, and track path resolution")
{
    auto io = IOSetup::createDefault (4, 6);
    REQUIRE (io.inputs.size() == 6);                 // 4 mono + 2 stereo pairs
    CHECK (io.inputs[4].name == "In 1-2");
    CHECK (io.inputs[4].numChannels == 2);
    REQUIRE (io.outputs.size() == 3);                // Main, Out 3-4, Out 5-6
    CHECK (io.outputs[0].name == "Main");
    CHECK (io.outputs[2].firstChannel == 4);
    CHECK (io.busName (0) == "Bus 1-2");

    Session s;
    io.busNames[0] = "Reverb";
    s.execute (std::make_unique<SetIOSetupCommand> (io));
    CHECK (s.busName (0) == "Reverb");
    CHECK (s.busName (1) == "Bus 3-4");

    Track a; a.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (a));
    s.execute (std::make_unique<SetTrackPathsCommand> (0, 4, 2));   // In 1-2, Out 5-6
    const auto& t = s.getTracks()[0];
    CHECK (t.inputPath == 4);
    CHECK (t.firstInput == 0);
    CHECK (t.numInputs == 2);
    CHECK (t.outputPath == 2);
    CHECK (s.resolveInput (t) == std::pair<int, int> (0, 2));

    auto snap = buildRenderSnapshot (s);
    CHECK (snap->strips[0].outputChannel == 4);
    CHECK (snap->mainOutputChannel == 0);

    // Removing the referenced paths falls back safely
    IOSetup smaller = IOSetup::createDefault (1, 2);
    s.execute (std::make_unique<SetIOSetupCommand> (smaller));
    CHECK (s.getTracks()[0].inputPath == -1);
    CHECK (s.getTracks()[0].outputPath == 0);
    s.undo();
    CHECK (s.getTracks()[0].inputPath == 4);
    CHECK (s.busName (0) == "Reverb");

    // The Main output can never be deleted
    IOSetup none; none.outputs.clear();
    s.execute (std::make_unique<SetIOSetupCommand> (none));
    REQUIRE (s.getIO().outputs.size() == 1);
    CHECK (s.getIO().outputs[0].name == "Main");
}
