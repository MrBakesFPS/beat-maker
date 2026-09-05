#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <AutomationRecorder.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
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
    std::shared_ptr<AutomationLane> lane (std::initializer_list<AutomationPoint> pts)
    {
        auto l = std::make_shared<AutomationLane>();
        l->param = ParamId::volume();
        l->points = pts;
        l->sortPoints();
        return l;
    }
}

TEST_CASE ("Master volume automation and trim scale the mix; Off and writing bypass it")
{
    Transport t;
    AudioGraph graph (t);
    auto make = [&] (bool read, bool bypass, float trim)
    {
        auto snap = std::make_unique<RenderSnapshot>();
        RenderClip c; c.audio = makeDc (100000, 1.0f); c.length = 100000; c.strip = 0;
        snap->clips.push_back (c);
        snap->strips.push_back ({});
        snap->master.gain = 0.5f;
        snap->master.automationRead = read;
        snap->master.trimGain = trim;
        snap->master.automation.push_back ({ lane ({ { 0, 0.25f } }), bypass });
        return snap;
    };
    juce::AudioBuffer<float> out (2, 64);

    graph.setSnapshot (make (true, false, 1.0f)); t.setPositionSamples (0); t.play();
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.25f, 1e-6));          // lane, not the fader

    graph.setSnapshot (make (false, false, 1.0f)); t.setPositionSamples (0);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f, 1e-6));           // Off: fader

    graph.setSnapshot (make (true, true, 1.0f)); t.setPositionSamples (0);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f, 1e-6));           // writing: live fader

    graph.setSnapshot (make (true, false, 2.0f)); t.setPositionSamples (0);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f, 1e-6));           // lane 0.25 x trim 2
    graph.collectGarbage();

    // Builder emits master lanes and trim
    Session s;
    s.execute (std::make_unique<ReplaceAutomationLaneCommand> (-1, lane ({ { 0, 0.7f } })));
    s.execute (std::make_unique<SetAutomationModeCommand> (-1, AutomationMode::trim));
    s.execute (std::make_unique<SetVolumeTrimCommand> (-1, 1.5f));
    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->master.automation.size() == 1);
    CHECK (snap->master.automationRead);
    CHECK_THAT (snap->master.trimGain, WithinAbs (1.5f, 1e-6));
    CHECK (s.getHistory().getUndoName() == "Automation");   // trim/mode not undoable
}

TEST_CASE ("Strip trim gain multiplies the volume lane only while automation is read")
{
    Transport t;
    AudioGraph graph (t);
    auto snap = std::make_unique<RenderSnapshot>();
    RenderClip c; c.audio = makeDc (100000, 1.0f); c.length = 100000; c.strip = 0;
    snap->clips.push_back (c);
    RenderStrip s; s.gain = 0.5f; s.trimGain = 2.0f; s.automationRead = true;
    s.automation.push_back ({ lane ({ { 0, 0.5f } }), false });
    snap->strips.push_back (s);
    graph.setSnapshot (std::move (snap));
    t.play();
    juce::AudioBuffer<float> out (2, 64);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (1.0f, 1e-6));   // lane 0.5 x trim 2

    auto snap2 = std::make_unique<RenderSnapshot>();
    snap2->clips.push_back (c);
    RenderStrip s2 = s; s2.automationRead = false;
    snap2->strips.push_back (s2);
    graph.setSnapshot (std::move (snap2));
    t.setPositionSamples (0);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f, 1e-6));   // Off ignores trim
    graph.collectGarbage();
}

TEST_CASE ("Recorder: static trim scales the whole lane in one undo step; a trim pass is baked relatively")
{
    Session s;
    Transport transport;
    transport.setSampleRate (1000.0);
    AutomationRecorder rec (s, transport);

    Track t; t.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (t));
    s.execute (std::make_unique<ReplaceAutomationLaneCommand> (0, lane ({ { 0, 1.0f }, { 4000, 0.5f }, { 8000, 1.0f } })));
    s.execute (std::make_unique<SetAutomationModeCommand> (0, AutomationMode::trim));

    // ---- Static trim while stopped: -6 dB over a gesture of several steps ----
    rec.trimChanged (0, -2.0f, true);
    rec.trimChanged (0, -4.0f, true);
    rec.trimChanged (0, -6.0206f, true);
    rec.gestureEnded (0, ParamId::volume());
    const auto* l = s.getTracks()[0].laneFor (ParamId::volume());
    REQUIRE (l != nullptr);
    CHECK_THAT (l->valueAt (0, 0.0f), WithinAbs (0.5f, 1e-4));
    CHECK_THAT (l->valueAt (4000, 0.0f), WithinAbs (0.25f, 1e-4));
    CHECK (s.getHistory().getUndoName() == "Trim Volume");
    s.undo();                                                   // one step restores the original
    CHECK_THAT (s.getTracks()[0].laneFor (ParamId::volume())->valueAt (0, 0.0f), WithinAbs (1.0f, 1e-6));
    CHECK (s.getHistory().getUndoName() == "Automation");

    // Trim needs an existing lane
    Track empty; empty.type = Track::Type::audio;
    s.execute (std::make_unique<AddTrackCommand> (empty));
    s.execute (std::make_unique<SetAutomationModeCommand> (1, AutomationMode::trim));
    rec.trimChanged (1, -3.0f, false);
    CHECK (s.getTracks()[1].laneFor (ParamId::volume()) == nullptr);

    // ---- Trim pass while playing: +6 dB from 2000 to 6000 ----
    transport.setPositionSamples (2000);
    transport.play();
    rec.tick();
    rec.trimChanged (0, 6.0206f, true);
    CHECK (rec.isTrimming (0));
    CHECK_THAT (s.getTracks()[0].volumeTrim, WithinAbs (2.0f, 1e-3));   // live trim for the engine
    rec.gestureEnded (0, ParamId::volume());
    CHECK (rec.isTrimming (0));                                          // trim latches until stop
    transport.setPositionSamples (6000);
    rec.tick();
    transport.stop();
    rec.tick();

    CHECK_FALSE (rec.isTrimming (0));
    CHECK_THAT (s.getTracks()[0].volumeTrim, WithinAbs (1.0f, 1e-6));    // live trim cleared
    l = s.getTracks()[0].laneFor (ParamId::volume());
    REQUIRE (l != nullptr);
    CHECK_THAT (l->valueAt (1000, 0.0f), WithinAbs (0.875f, 1e-3));      // before the pass: original ramp 1.0 -> 0.5
    CHECK_THAT (l->valueAt (2000, 0.0f), WithinAbs (0.75f * 2.0f, 1e-3)); // trimmed: original 0.75 x 2
    CHECK_THAT (l->valueAt (4000, 0.0f), WithinAbs (0.5f * 2.0f, 1e-3));  // existing point scaled
    CHECK_THAT (l->valueAt (6000, 0.0f), WithinAbs (0.75f * 2.0f, 1e-3)); // end of pass
    CHECK_THAT (l->valueAt (6001, 0.0f), WithinAbs (0.75f, 2e-3));        // back to the untrimmed data
    CHECK_THAT (l->valueAt (8000, 0.0f), WithinAbs (1.0f, 1e-6));         // later point untouched
    CHECK (s.getHistory().getUndoName() == "Trim Volume");
    s.undo();
    CHECK_THAT (s.getTracks()[0].laneFor (ParamId::volume())->valueAt (4000, 0.0f), WithinAbs (0.5f, 1e-6));
}

TEST_CASE ("Recorder writes and reads the master as track -1")
{
    Session s;
    Transport transport;
    transport.setSampleRate (1000.0);
    AutomationRecorder rec (s, transport);

    s.execute (std::make_unique<SetAutomationModeCommand> (-1, AutomationMode::write));
    s.execute (std::make_unique<SetTrackMixCommand> (-1, 0.6f, 0.0f));
    transport.setPositionSamples (500);
    transport.play();
    rec.tick();                                    // Write: master volume pass starts at play
    CHECK (rec.getNumActivePasses() == 1);
    transport.setPositionSamples (1500);
    rec.tick();
    transport.stop();
    rec.tick();
    const auto* l = s.getMaster().laneFor (ParamId::volume());
    REQUIRE (l != nullptr);
    CHECK_THAT (l->valueAt (1000, 0.0f), WithinAbs (0.6f, 1e-6));
    CHECK (s.getMaster().automationMode == AutomationMode::latch);   // dropped from Write

    s.execute (std::make_unique<SetAutomationModeCommand> (-1, AutomationMode::read));
    transport.setPositionSamples (1000);
    CHECK_THAT (*rec.displayedValue (-1, ParamId::volume()), WithinAbs (0.6f, 1e-6));
}
