#include "AutomationRecorder.h"

namespace beatmaker::model
{

AutomationRecorder::AutomationRecorder (Session& s, engine::Transport& t) : session (s), transport (t) {}

juce::int64 AutomationRecorder::now() const noexcept { return transport.getPositionSamples(); }

float AutomationRecorder::currentValue (const Track& t, const engine::ParamId& p)
{
    switch (p.type)
    {
        case engine::ParamId::Type::volume:    return t.gain;
        case engine::ParamId::Type::pan:       return t.pan;
        case engine::ParamId::Type::mute:      return t.mute ? 1.0f : 0.0f;
        case engine::ParamId::Type::sendLevel: return juce::isPositiveAndBelow (p.index, Track::numSendSlots) ? t.sends[(size_t) p.index].gain : 0.0f;
        case engine::ParamId::Type::insertParam:
            if (juce::isPositiveAndBelow (p.index, Track::numInsertSlots))
                if (const auto& ins = t.inserts[(size_t) p.index]; ins.params != nullptr && juce::isPositiveAndBelow (p.sub, (int) ins.params->values.size()))
                    return ins.params->values[(size_t) p.sub];
            return 0.0f;
    }
    return 0.0f;
}

bool AutomationRecorder::isWriting (int trackIndex, const engine::ParamId& p) const
{
    return passes.count ({ trackIndex, p }) > 0;
}

std::optional<float> AutomationRecorder::displayedValue (int trackIndex, const engine::ParamId& p) const
{
    auto* t = session.getTrack (trackIndex);
    if (t == nullptr || t->automationMode == AutomationMode::off || isWriting (trackIndex, p)) return std::nullopt;
    auto* lane = t->laneFor (p);
    if (lane == nullptr || lane->isEmpty()) return std::nullopt;
    return lane->valueAt (now(), currentValue (*t, p));
}

void AutomationRecorder::beginPass (const Key& key, AutomationMode mode, float value, bool gestureActive)
{
    Pass pass;
    pass.mode = mode;
    pass.start = now();
    pass.lastValue = value;
    pass.gestureActive = gestureActive;
    pass.points.push_back ({ pass.start, value });
    passes[key] = std::move (pass);
    session.execute (std::make_unique<SetAutomationWritingCommand> (key.track, key.param, true));
}

void AutomationRecorder::parameterChanged (int trackIndex, const engine::ParamId& param, float value, bool gestureActive)
{
    auto* t = session.getTrack (trackIndex);
    if (t == nullptr || ! transport.isPlaying()) return;

    const auto mode = t->automationMode;
    if (mode != AutomationMode::touch && mode != AutomationMode::latch && mode != AutomationMode::write) return;

    const Key key { trackIndex, param };
    auto it = passes.find (key);
    if (it == passes.end())
    {
        beginPass (key, mode, value, gestureActive);
        return;
    }

    auto& pass = it->second;
    pass.gestureActive = gestureActive;
    pass.lastValue = value;
    const auto time = now();
    if (! pass.points.empty() && pass.points.back().time == time) pass.points.back().value = value;
    else pass.points.push_back ({ time, value });
}

void AutomationRecorder::gestureEnded (int trackIndex, const engine::ParamId& param)
{
    const Key key { trackIndex, param };
    auto it = passes.find (key);
    if (it == passes.end()) return;
    it->second.gestureActive = false;
    if (it->second.mode == AutomationMode::touch)
    {
        finishPass (key, it->second, now());
        passes.erase (it);
    }
}

void AutomationRecorder::tick()
{
    const bool playing = transport.isPlaying();

    if (playing)
    {
        lastKnownPosition = now();

        // Write mode: every track in Write starts writing volume and pan as soon as playback begins.
        if (! wasPlaying)
            for (int i = 0; i < session.getNumTracks(); ++i)
            {
                const auto& t = session.getTracks()[(size_t) i];
                if (t.automationMode != AutomationMode::write) continue;
                for (auto p : { engine::ParamId::volume(), engine::ParamId::pan() })
                    if (! passes.count ({ i, p }))
                        beginPass ({ i, p }, AutomationMode::write, currentValue (t, p), false);
            }

        // Keep latched/written values current so the lane holds the last value.
        for (auto& [key, pass] : passes)
            if (! pass.points.empty() && pass.points.back().time < lastKnownPosition)
                pass.points.push_back ({ lastKnownPosition, pass.lastValue });
    }
    else if (wasPlaying)
    {
        finishAll (lastKnownPosition);
    }

    wasPlaying = playing;
}

void AutomationRecorder::finishAll (juce::int64 end)
{
    auto copy = std::move (passes);
    passes.clear();
    for (auto& [key, pass] : copy)
        finishPass (key, pass, end);
}

void AutomationRecorder::finishPass (const Key& key, Pass& pass, juce::int64 end)
{
    auto* t = session.getTrack (key.track);
    if (t == nullptr) { session.execute (std::make_unique<SetAutomationWritingCommand> (key.track, key.param, false)); return; }

    end = juce::jmax (end, pass.start);
    const auto* existing = t->laneFor (key.param);
    const float fallback = currentValue (*t, key.param);

    auto lane = std::make_shared<engine::AutomationLane>();
    lane->param = key.param;

    // Existing data before the pass, plus an anchor just before it so the
    // ramp into the written section starts from the old value.
    if (existing != nullptr)
    {
        for (const auto& p : existing->points) if (p.time < pass.start) lane->points.push_back (p);
        if (pass.start > 0) lane->points.push_back ({ pass.start - 1, existing->valueAt (pass.start - 1, fallback) });
    }

    // The pass itself (thin exact duplicates)
    for (const auto& p : pass.points)
        if (p.time >= pass.start && p.time <= end)
            if (lane->points.empty() || lane->points.back().time != p.time || std::abs (lane->points.back().value - p.value) > 1.0e-7f)
                lane->points.push_back (p);
    if (lane->points.empty() || lane->points.back().time < end)
        lane->points.push_back ({ end, pass.lastValue });

    // After the pass: Touch returns to the existing data; Latch/Write keep
    // the written value (existing points after the pass are kept, so the
    // lane ramps back to them if any).
    if (existing != nullptr)
    {
        if (pass.mode == AutomationMode::touch)
            lane->points.push_back ({ end + 1, existing->valueAt (end + 1, fallback) });
        for (const auto& p : existing->points) if (p.time > end + 1) lane->points.push_back (p);
    }

    lane->sortPoints();

    const auto name = juce::String (automationModeName (pass.mode)) + " " + key.param.getName();
    session.execute (std::make_unique<SetAutomationWritingCommand> (key.track, key.param, false));
    session.execute (std::make_unique<ReplaceAutomationLaneCommand> (key.track, lane, name));

    // Pro Tools drops Write to Latch after a pass so nothing is overwritten by accident.
    if (pass.mode == AutomationMode::write && t->automationMode == AutomationMode::write)
        session.execute (std::make_unique<SetAutomationModeCommand> (key.track, AutomationMode::latch));
}

} // namespace beatmaker::model
