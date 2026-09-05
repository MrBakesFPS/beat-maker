#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <GroupLogic.h>
#include <RenderSnapshotBuilder.h>
#include <Session.h>
#include <graph/AudioGraph.h>

using namespace beatmaker::model;
using Catch::Matchers::WithinAbs;

namespace
{
    void addAudio (Session& s, const char* name) { Track t; t.name = name; t.type = Track::Type::audio; s.execute (std::make_unique<AddTrackCommand> (t)); }

    AudioClip clipAt (juce::int64 start, juce::int64 len)
    {
        AudioClip c; c.audio = std::make_shared<const juce::AudioBuffer<float>> (1, 100000); c.sampleRate = 48000.0; c.timelineStart = start; c.length = len; return c;
    }

    std::shared_ptr<const juce::AudioBuffer<float>> makeDc (int length, float value)
    {
        juce::AudioBuffer<float> b (1, length);
        juce::FloatVectorOperations::fill (b.getWritePointer (0), value, length);
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }
}

TEST_CASE ("Group commands: create/replace/delete are undoable, enable is not, badges follow ids")
{
    Session s;
    addAudio (s, "A"); addAudio (s, "B"); addAudio (s, "C");

    Group g; g.name = "Drums"; g.trackIds = { 1, 2 };
    auto create = std::make_unique<CreateGroupCommand> (g);
    auto* raw = create.get();
    s.execute (std::move (create));
    REQUIRE (s.getGroups().size() == 1);
    const int id = raw->getGroupId();
    CHECK (id == 1);
    CHECK (s.getGroup (id)->badge() == "a");
    CHECK (s.getGroup (id)->contains (2));
    CHECK_FALSE (s.getGroup (id)->contains (3));

    s.execute (std::make_unique<SetGroupActiveCommand> (id, false));
    CHECK_FALSE (s.getGroup (id)->active);
    CHECK (s.getHistory().getUndoName() == "Create Group");

    auto edited = *s.getGroup (id);
    edited.name = "Rhythm"; edited.trackIds = { 1, 2, 3 }; edited.type = Group::Type::mix;
    s.execute (std::make_unique<ReplaceGroupCommand> (edited));
    CHECK (s.getGroup (id)->name == "Rhythm");
    CHECK (s.getGroup (id)->contains (3));
    s.undo();
    CHECK (s.getGroup (id)->name == "Drums");

    s.execute (std::make_unique<RemoveGroupCommand> (id));
    CHECK (s.getGroups().empty());
    s.undo();
    REQUIRE (s.getGroups().size() == 1);
    CHECK (s.getGroup (id)->name == "Drums");
    s.undo();   // undo create
    CHECK (s.getGroups().empty());
}

TEST_CASE ("Mix groups move faders relatively in dB and follow mute/solo/pan per attribute")
{
    Session s;
    addAudio (s, "A"); addAudio (s, "B"); addAudio (s, "C");
    s.execute (std::make_unique<SetTrackMixCommand> (1, 0.5f, 0.0f));   // B at -6 dB
    Group g; g.trackIds = { 1, 2 }; g.type = Group::Type::mix; g.attributes.pan = false;
    s.execute (std::make_unique<CreateGroupCommand> (g));

    // A +6 dB (1.0 -> 2.0): B rises +6 dB too (0.5 -> 1.0); C is not in the group
    s.execute (GroupLogic::mixCommand (s, 0, 2.0f, 0.0f));
    CHECK_THAT (s.getTracks()[0].gain, WithinAbs (2.0f, 1e-6));
    CHECK_THAT (s.getTracks()[1].gain, WithinAbs (1.0f, 1e-4));
    CHECK_THAT (s.getTracks()[2].gain, WithinAbs (1.0f, 1e-6));
    CHECK (s.getHistory().getUndoName() == "Grouped Volume/Pan");
    s.undo();
    CHECK_THAT (s.getTracks()[1].gain, WithinAbs (0.5f, 1e-6));

    // Pan doesn't follow unless the attribute says so
    s.execute (GroupLogic::mixCommand (s, 0, 1.0f, -1.0f));
    CHECK_THAT (s.getTracks()[0].pan, WithinAbs (-1.0f, 1e-6));
    CHECK_THAT (s.getTracks()[1].pan, WithinAbs (0.0f, 1e-6));

    auto withPan = *s.getGroup (1); withPan.attributes.pan = true;
    s.execute (std::make_unique<ReplaceGroupCommand> (withPan));
    s.execute (GroupLogic::mixCommand (s, 0, 1.0f, 0.5f));
    CHECK_THAT (s.getTracks()[1].pan, WithinAbs (0.5f, 1e-6));

    // Fader to -inf takes the group to -inf; from -inf a member stays there
    s.execute (GroupLogic::mixCommand (s, 0, 0.0f, 0.5f));
    CHECK (s.getTracks()[1].gain == 0.0f);
    s.execute (GroupLogic::mixCommand (s, 0, 1.0f, 0.5f));
    CHECK (s.getTracks()[1].gain == 0.0f);

    // Mute / solo propagate; an inactive group stops following
    s.execute (GroupLogic::flagCommand (s, 1, SetTrackFlagCommand::Flag::mute, true));
    CHECK (s.getTracks()[0].mute);
    CHECK_FALSE (s.getTracks()[2].mute);
    s.execute (std::make_unique<SetGroupActiveCommand> (1, false));
    s.execute (GroupLogic::flagCommand (s, 1, SetTrackFlagCommand::Flag::solo, true));
    CHECK (s.getTracks()[1].solo);
    CHECK_FALSE (s.getTracks()[0].solo);

    // Edit-only groups never touch the mix
    s.execute (std::make_unique<SetGroupActiveCommand> (1, true));
    auto editOnly = *s.getGroup (1); editOnly.type = Group::Type::edit;
    s.execute (std::make_unique<ReplaceGroupCommand> (editOnly));
    CHECK (GroupLogic::mixMembers (s, 0).size() == 1);
    CHECK (GroupLogic::editMembers (s, 0).size() == 2);
}

TEST_CASE ("Edit groups: same-start clips on member tracks move, trim and delete together")
{
    Session s;
    addAudio (s, "A"); addAudio (s, "B"); addAudio (s, "C");
    s.execute (std::make_unique<AddClipCommand> (0, clipAt (1000, 500)));
    s.execute (std::make_unique<AddClipCommand> (1, clipAt (1000, 800)));   // sibling (same start)
    s.execute (std::make_unique<AddClipCommand> (1, clipAt (5000, 300)));   // not a sibling
    s.execute (std::make_unique<AddClipCommand> (2, clipAt (1000, 500)));   // not in the group
    Group g; g.trackIds = { 1, 2 }; g.type = Group::Type::edit;
    s.execute (std::make_unique<CreateGroupCommand> (g));

    const ClipRef a { 0, ClipRef::Kind::audio, 0 };
    const auto sibs = GroupLogic::siblingsOf (s, a);
    REQUIRE (sibs.size() == 1);
    CHECK (sibs[0] == ClipRef { 1, ClipRef::Kind::audio, 0 });

    s.execute (GroupLogic::moveCommand (s, a, 0, 3000));
    CHECK (s.getTracks()[0].clips[0].timelineStart == 3000);
    CHECK (s.getTracks()[1].clips[0].timelineStart == 3000);
    CHECK (s.getTracks()[1].clips[1].timelineStart == 5000);
    CHECK (s.getTracks()[2].clips[0].timelineStart == 1000);
    s.undo();
    CHECK (s.getTracks()[1].clips[0].timelineStart == 1000);

    s.execute (GroupLogic::trimCommand (s, a, 1200, 300));   // start +200, end 0
    CHECK (s.getTracks()[0].clips[0].timelineStart == 1200);
    CHECK (s.getTracks()[0].clips[0].length == 300);
    CHECK (s.getTracks()[1].clips[0].timelineStart == 1200);
    CHECK (s.getTracks()[1].clips[0].length == 600);        // 1200..1800: same start delta, own end
    s.undo();

    // Moving across tracks is never propagated
    s.execute (GroupLogic::moveCommand (s, a, 2, 7000));
    CHECK (s.getTracks()[1].clips[0].timelineStart == 1000);
    s.undo();
}

TEST_CASE ("VCA masters scale members' faders (including volume automation), mute and solo them, and carry no audio")
{
    using namespace beatmaker::engine;
    Session s;
    addAudio (s, "A"); addAudio (s, "B");
    Track vca; vca.name = "VCA"; vca.type = Track::Type::vca;
    s.execute (std::make_unique<AddTrackCommand> (vca));
    const int vcaId = s.getTracks()[2].id;

    s.execute (std::make_unique<SetTrackVcaCommand> (0, vcaId));
    CHECK (s.getTracks()[0].vcaTrackId == vcaId);
    s.execute (std::make_unique<SetTrackVcaCommand> (1, 999));      // unknown -> none
    CHECK (s.getTracks()[1].vcaTrackId == -1);
    s.execute (std::make_unique<SetTrackVcaCommand> (2, vcaId));    // a VCA can't follow a VCA
    CHECK (s.getTracks()[2].vcaTrackId == -1);

    s.execute (std::make_unique<AddClipCommand> (0, [] { AudioClip c; c.audio = makeDc (10000, 0.5f); c.sampleRate = 48000.0; c.length = 10000; return c; }()));
    s.execute (std::make_unique<AddClipCommand> (1, [] { AudioClip c; c.audio = makeDc (10000, 0.5f); c.sampleRate = 48000.0; c.length = 10000; return c; }()));
    s.execute (std::make_unique<SetTrackMixCommand> (2, 0.5f, 0.0f));   // VCA at -6 dB

    auto snap = buildRenderSnapshot (s);
    REQUIRE (snap->strips.size() == 3);
    CHECK (snap->strips[0].vcaStrip == 2);
    CHECK (snap->strips[1].vcaStrip == -1);
    CHECK (snap->strips[2].isVca);
    CHECK (snap->strips[2].muted);

    Transport t; t.setSampleRate (48000.0);
    AudioGraph graph (t);
    graph.setSnapshot (std::move (snap));
    t.play();
    juce::AudioBuffer<float> out (2, 64);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f * 0.5f + 0.5f, 1e-6));   // A scaled by the VCA, B not

    // VCA mute mutes A only; VCA solo solos A
    s.execute (std::make_unique<SetTrackFlagCommand> (2, SetTrackFlagCommand::Flag::mute, true));
    snap = buildRenderSnapshot (s);
    CHECK (snap->strips[0].muted);
    CHECK_FALSE (snap->strips[1].muted);
    s.undo();
    s.execute (std::make_unique<SetTrackFlagCommand> (2, SetTrackFlagCommand::Flag::solo, true));
    snap = buildRenderSnapshot (s);
    CHECK_FALSE (snap->strips[0].muted);
    CHECK (snap->strips[1].muted);
    s.undo();

    // VCA volume automation scales the member at block rate
    auto lane = std::make_shared<AutomationLane>();
    lane->param = ParamId::volume();
    lane->points = { { 0, 2.0f } };
    s.execute (std::make_unique<ReplaceAutomationLaneCommand> (2, lane));
    graph.setSnapshot (buildRenderSnapshot (s));
    t.setPositionSamples (0);
    graph.renderBlock (out.getArrayOfWritePointers(), 2, 64);
    CHECK_THAT (out.getSample (0, 10), WithinAbs (0.5f * 2.0f + 0.5f, 1e-6));
    graph.collectGarbage();
}
