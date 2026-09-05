// AutomationRecorder: turns live parameter changes during playback into
// automation passes, following Pro Tools semantics:
//   Touch  - writes while the control is held, then returns to existing data
//   Latch  - writes from the first touch until the transport stops
//   Write  - writes every enabled parameter from play start until stop
//            (then drops to Latch, as Pro Tools does for safety)
// A pass becomes one undoable ReplaceAutomationLaneCommand when it ends.
#pragma once

#include "Session.h"
#include <transport/Transport.h>
#include <map>

namespace beatmaker::model
{

class AutomationRecorder
{
public:
    AutomationRecorder (Session& session, engine::Transport& transport);

    // Call whenever a mixer control changes value (after the model command).
    void parameterChanged (int trackIndex, const engine::ParamId&, float value, bool gestureActive);
    // Call when the user releases a control.
    void gestureEnded (int trackIndex, const engine::ParamId&);

    // Trim mode: the fader reports an offset in dB relative to where the
    // gesture started. Playing: recorded as a trim pass and baked into the
    // volume lane on stop. Stopped: the whole lane is scaled (one undo step).
    void trimChanged (int trackIndex, float trimDb, bool gestureActive);
    bool isTrimming (int trackIndex) const;
    // Call from a timer: detects play start (Write mode) and stop (ends passes).
    void tick();

    bool isWriting (int trackIndex, const engine::ParamId&) const;
    int getNumActivePasses() const noexcept { return (int) passes.size(); }

    // The value a mixer control should display: the automated value when the
    // track reads automation and the control is not being written.
    std::optional<float> displayedValue (int trackIndex, const engine::ParamId&) const;

private:
    struct Key { int track; engine::ParamId param; bool operator< (const Key& o) const noexcept { return track != o.track ? track < o.track : param < o.param; } };
    struct Pass
    {
        AutomationMode mode;
        juce::int64 start = 0;
        std::vector<engine::AutomationPoint> points;   // Trim passes store the trim in dB
        float lastValue = 0.0f;
        bool gestureActive = false;
    };
    struct StaticTrim
    {
        std::shared_ptr<const engine::AutomationLane> base;
        bool issued = false;
    };

    juce::int64 now() const noexcept;
    void beginPass (const Key&, AutomationMode, float value, bool gestureActive);
    void finishPass (const Key&, Pass&, juce::int64 end);
    void finishTrimPass (const Key&, Pass&, juce::int64 end);
    void finishAll (juce::int64 end);
    static float currentValue (const Track&, const engine::ParamId&);

    Session& session;
    engine::Transport& transport;
    std::map<Key, Pass> passes;
    std::map<int, StaticTrim> staticTrims;   // by track index, while stopped
    bool wasPlaying = false;
    juce::int64 lastKnownPosition = 0;
};

} // namespace beatmaker::model
