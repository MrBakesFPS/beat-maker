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
        std::vector<engine::AutomationPoint> points;
        float lastValue = 0.0f;
        bool gestureActive = false;
    };

    juce::int64 now() const noexcept;
    void beginPass (const Key&, AutomationMode, float value, bool gestureActive);
    void finishPass (const Key&, Pass&, juce::int64 end);
    void finishAll (juce::int64 end);
    static float currentValue (const Track&, const engine::ParamId&);

    Session& session;
    engine::Transport& transport;
    std::map<Key, Pass> passes;
    bool wasPlaying = false;
    juce::int64 lastKnownPosition = 0;
};

} // namespace beatmaker::model
