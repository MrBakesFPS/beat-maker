#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <AutomationRecorder.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <automation/Automation.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::engine;
using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    std::shared_ptr<const juce::AudioBuffer<float>> makeDc (int length, float value)
    {
        juce::AudioBuffer<float> b (1, length);
        juce::FloatVectorOperations::fill (b.getWritePointer (0), value, length);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }

    std::shared_ptr<AutomationLane> lane (ParamId p, std::initializer_list<AutomationPoint> pts)
    {
        auto l = std::make_shared<AutomationLane>();
        l->param = p;
        l->points = pts;
        l->sortPoints();
        return l;
    }
}

TEST_CASE ("Lane evaluation interpolates, clamps at the ends, and steps for mute")
{
    auto vol = lane (ParamId::volume(), { { 100, 0.0f }, { 200, 1.0f } });
    CHECK_THAT (vol->valueAt (0, 0.5f), WithinAbs (0.0f, 1e-6));       // before first
    CHECK_THAT (vol->valueAt (150, 0.5f), WithinAbs (0.5f, 1e-6));     // midway
    CHECK_THAT (vol->valueAt (175, 0.5f), WithinAbs (0.75f, 1e-6));
    CHECK_THAT (vol->valueAt (999, 0.5f), WithinAbs (1.0f, 1e-6));     // after last

    AutomationLane empty; empty.param = ParamId::pan();
    CHECK_THAT (empty.valueAt (50, 0.3f), WithinAbs (0.3f, 1e-6));     // fallback

    auto mute = lane (ParamId::mute(), { { 100, 0.0f }, { 200, 1.0f } });
    CHECK_THAT (mute->valueAt (150, 0.0f), WithinAbs (0.0f, 1e-6));    // stepped: holds until the next point
    CHECK_THAT (mute->valueAt (200, 0.0f), WithinAbs (1.0f, 1e-6));

    CHECK (ParamId::send (2).getName() == "Send C");
    CHECK (ParamId::insert (1, 3).getName() == "Insert 2 / 4");
    CHECK (ParamId::volume() < ParamId::pan());
}

TEST_CASE ("Graph reads volume (ramped), pan, mute, send and insert automation; Off and writing bypass it")
{
    Transport t;
    AudioGraph graph (t);

    auto makeSnap = [&] (bool read, bool bypassVolume)
    {
        auto snap = std::make_unique<RenderSnapshot>();
        RenderClip c; c.audio = makeDc (100000, 1.0f); c.length = 100000; c.strip = 0;
        snap->clips.push_back (c);
        RenderStrip s;
        s.gain = 0.5f;
        s.automationRead = read;
        s.automation.push_back ({ lane (ParamId::volume(), { { 0, 0.0f }, { 1000, 1.0f } }), bypassVolume });
        s.automation.push_back ({ lane (ParamId::pan(), { { 0, -1.0f } }), false });
        s.automation.push_back ({ lane (ParamId::mute(), { { 0, 0.0f }, { 5000, 1.0f } }), false });
        s.sends.push_back ({ 0, 0.25f, true, 1 });
        s.automation.push_back ({ lane (ParamId::send (1), { { 0, 1.0f } }), false });
        snap->strips.push_back (s);
        RenderStrip aux; aux.isAux = true; aux.inputBus = 0; aux.outputBus = 7;   // park the send somewhere we can meter
        snap->strips.push_back (aux);
        return snap;
    };

    // Read: volume ramps 0 -> 1 over the first 1000 samples, hard left, send at 1.0 (automated) not 0.25
    graph.setSnapshot (makeSnap (true, false));
    t.setPositionSamples (0);
    t.play();
    juce::AudioBuffer<float> out (2, 500);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 500);
    CHECK_THAT (out.getSample (0, 0), WithinAbs (0.0f, 1e-4));
    CHECK_THAT (out.getSample (0, 250), WithinAbs (0.25f * juce::MathConstants<float>::sqrt2, 2e-3));   // ramp midpoint of the block, hard left
    CHECK_THAT (out.getSample (1, 250), WithinAbs (0.0f, 1e-6));
    CHECK_THAT (graph.getStripPeak (1, 0), WithinAbs (1.0f, 1e-6));    // pre-fader send at automated 1.0 -> aux meter

    // Past 5000 the mute lane kicks in
    t.setPositionSamples (6000);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 500);
    CHECK (out.getMagnitude (0, 0, 500) == 0.0f);

    // Off: static values (gain 0.5, centre, send 0.25, not muted)
    graph.setSnapshot (makeSnap (false, false));
    t.setPositionSamples (6000);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 500);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f, 1e-6));
    CHECK_THAT (out.getSample (1, 10), WithinAbs (0.5f, 1e-6));
    CHECK_THAT (graph.getStripPeak (1, 0), WithinAbs (0.25f, 1e-6));

    // Writing volume: the lane is bypassed, the live fader value (0.5) is used, other lanes still read
    graph.setSnapshot (makeSnap (true, true));
    t.setPositionSamples (0);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 500);
    CHECK_THAT (out.getSample (0, 100), WithinAbs (0.5f * juce::MathConstants<float>::sqrt2, 1e-5));
    graph.collectGarbage();
}

TEST_CASE ("Insert parameter automation overrides the stored value per block")
{
    Transport t;
    t.setSampleRate (44100.0);
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip c; c.audio = makeDc (10000, 0.1f); c.length = 10000; c.strip = 0;
    snap->clips.push_back (c);
    RenderStrip s;
    auto fx = std::shared_ptr<Effect> (Effect::create (EffectType::compressor, 44100.0));
    auto p = std::make_shared<const InsertParams> ([] { auto d = Effect::defaultParams (EffectType::compressor); d.values[CompressorEffect::threshold] = 0.0f; return d; }());
    s.inserts.push_back ({ fx, p, false, /*slot*/ 3 });
    s.automationRead = true;
    s.automation.push_back ({ lane (ParamId::insert (3, CompressorEffect::makeup), { { 0, 6.0206f } }), false });   // +6 dB makeup
    snap->strips.push_back (s);
    graph.setSnapshot (std::move (snap));
    t.play();
    juce::AudioBuffer<float> out (2, 1024);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 1024);
    CHECK_THAT (out.getSample (0, 1000), WithinAbs (0.2f, 1e-3));
    graph.collectGarbage();
}

TEST_CASE ("Lane and mode commands: replace is undoable, mode and writing are not")
{
    Session s;
    Track t; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));

    s.execute (std::make_unique<ReplaceAutomationLaneCommand> (0, lane (ParamId::volume(), { { 0, 1.0f }, { 100, 0.5f } })));
    REQUIRE (s.getTracks()[0].laneFor (ParamId::volume()) != nullptr);
    CHECK (s.getHistory().getUndoName() == "Automation");

    s.execute (std::make_unique<SetAutomationModeCommand> (0, AutomationMode::touch));
    CHECK (s.getTracks()[0].automationMode == AutomationMode::touch);
    s.execute (std::make_unique<SetAutomationWritingCommand> (0, ParamId::volume(), true));
    CHECK (s.getTracks()[0].isWriting (ParamId::volume()));
    CHECK (s.getHistory().getUndoName() == "Automation");   // unchanged: not undoable

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->strips[0].automation.size() == 1);
    CHECK (snap->strips[0].automation[0].bypass);           // writing
    CHECK (snap->strips[0].automationRead);

    s.execute (std::make_unique<SetAutomationWritingCommand> (0, ParamId::volume(), false));
    CHECK_FALSE (buildRenderSnapshot (s)->strips[0].automation[0].bypass);

    s.undo();
    CHECK (s.getTracks()[0].laneFor (ParamId::volume()) == nullptr);
    s.redo();
    CHECK (s.getTracks()[0].laneFor (ParamId::volume()) != nullptr);

    // An empty lane removes the parameter's automation
    s.execute (std::make_unique<ReplaceAutomationLaneCommand> (0, std::make_shared<AutomationLane> (AutomationLane { ParamId::volume(), {} })));
    CHECK (s.getTracks()[0].laneFor (ParamId::volume()) == nullptr);
}

TEST_CASE ("Recorder: Touch writes while held and returns to existing data; Latch holds to stop; Write starts at play and drops to Latch")
{
    Session s;
    Transport transport;
    transport.setSampleRate (1000.0);   // 1 sample = 1 ms, easy numbers
    AutomationRecorder rec (s, transport);

    Track t; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));
    // Existing lane: 1.0 throughout, dipping to 0.2 at 5000
    s.execute (std::make_unique<ReplaceAutomationLaneCommand> (0, lane (ParamId::volume(), { { 0, 1.0f }, { 5000, 0.2f } })));

    // ---- Touch ----
    s.execute (std::make_unique<SetAutomationModeCommand> (0, AutomationMode::touch));
    rec.parameterChanged (0, ParamId::volume(), 0.7f, true);   // stopped: ignored
    CHECK (rec.getNumActivePasses() == 0);

    transport.setPositionSamples (1000);
    transport.play();
    rec.tick();
    s.execute (std::make_unique<SetTrackMixCommand> (0, 0.7f, 0.0f));
    rec.parameterChanged (0, ParamId::volume(), 0.7f, true);
    CHECK (rec.isWriting (0, ParamId::volume()));
    CHECK (s.getTracks()[0].isWriting (ParamId::volume()));
    CHECK_FALSE (rec.displayedValue (0, ParamId::volume()).has_value());   // writing: show the live value

    transport.setPositionSamples (1500);
    s.execute (std::make_unique<SetTrackMixCommand> (0, 0.4f, 0.0f));
    rec.parameterChanged (0, ParamId::volume(), 0.4f, true);
    transport.setPositionSamples (2000);
    rec.gestureEnded (0, ParamId::volume());

    CHECK (rec.getNumActivePasses() == 0);
    CHECK_FALSE (s.getTracks()[0].isWriting (ParamId::volume()));
    const auto* l = s.getTracks()[0].laneFor (ParamId::volume());
    REQUIRE (l != nullptr);
    CHECK_THAT (l->valueAt (500, 0.0f), WithinAbs (0.92f, 1e-6));      // before: the old ramp, untouched
    CHECK_THAT (l->valueAt (1000, 0.0f), WithinAbs (0.7f, 1e-6));      // touch start
    CHECK_THAT (l->valueAt (1500, 0.0f), WithinAbs (0.4f, 1e-6));
    CHECK_THAT (l->valueAt (2000, 0.0f), WithinAbs (0.4f, 1e-6));      // held to release
    CHECK_THAT (l->valueAt (2001, 0.0f), WithinAbs (1.0f - 0.8f * 2001.0f / 5000.0f, 1e-3));   // back on the old ramp
    CHECK_THAT (l->valueAt (5000, 0.0f), WithinAbs (0.2f, 1e-6));      // existing later point kept
    CHECK (s.getHistory().getUndoName() == "Touch Volume");

    // Read display follows the lane
    transport.setPositionSamples (1500);
    s.execute (std::make_unique<SetAutomationModeCommand> (0, AutomationMode::read));
    CHECK_THAT (*rec.displayedValue (0, ParamId::volume()), WithinAbs (0.4f, 1e-6));

    // ---- Latch: keeps writing after release until stop ----
    s.execute (std::make_unique<SetAutomationModeCommand> (0, AutomationMode::latch));
    transport.setPositionSamples (3000);
    rec.tick();
    s.execute (std::make_unique<SetTrackMixCommand> (0, 0.9f, 0.0f));
    rec.parameterChanged (0, ParamId::volume(), 0.9f, true);
    rec.gestureEnded (0, ParamId::volume());
    CHECK (rec.getNumActivePasses() == 1);            // still latched
    transport.setPositionSamples (6000);
    rec.tick();
    transport.stop();
    rec.tick();                                        // stop ends the pass at 6000
    CHECK (rec.getNumActivePasses() == 0);
    l = s.getTracks()[0].laneFor (ParamId::volume());
    CHECK_THAT (l->valueAt (4000, 0.0f), WithinAbs (0.9f, 1e-6));
    CHECK_THAT (l->valueAt (6000, 0.0f), WithinAbs (0.9f, 1e-6));
    CHECK_THAT (l->valueAt (2999, 0.0f), WithinAbs (1.0f - 0.8f * 2999.0f / 5000.0f, 1e-3));   // old data before the pass

    // ---- Write: starts on play without touching, then drops to Latch ----
    s.execute (std::make_unique<SetAutomationModeCommand> (0, AutomationMode::write));
    s.execute (std::make_unique<SetTrackMixCommand> (0, 0.3f, 0.5f));
    transport.setPositionSamples (7000);
    transport.play();
    rec.tick();
    CHECK (rec.getNumActivePasses() == 2);            // volume + pan
    transport.setPositionSamples (8000);
    rec.tick();
    transport.stop();
    rec.tick();
    CHECK (s.getTracks()[0].automationMode == AutomationMode::latch);
    l = s.getTracks()[0].laneFor (ParamId::volume());
    CHECK_THAT (l->valueAt (7500, 0.0f), WithinAbs (0.3f, 1e-6));
    const auto* pl = s.getTracks()[0].laneFor (ParamId::pan());
    REQUIRE (pl != nullptr);
    CHECK_THAT (pl->valueAt (7500, 0.0f), WithinAbs (0.5f, 1e-6));

    // Each pass was a single undo step
    s.undo();   // pan write
    CHECK (s.getTracks()[0].laneFor (ParamId::pan()) == nullptr);
}
