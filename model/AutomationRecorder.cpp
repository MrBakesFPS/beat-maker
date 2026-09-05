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
    auto* t = session.getTrackOrMaster (trackIndex);
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

bool AutomationRecorder::isTrimming (int trackIndex) const
{
    return passes.count ({ trackIndex, engine::ParamId::volume() }) > 0
        && passes.at ({ trackIndex, engine::ParamId::volume() }).mode == AutomationMode::trim;
}

void AutomationRecorder::trimChanged (int trackIndex, float trimDb, bool gestureActive)
{
    auto* t = session.getTrackOrMaster (trackIndex);
    if (t == nullptr || t->automationMode != AutomationMode::trim) return;
    const auto param = engine::ParamId::volume();
    const auto* lane = t->laneFor (param);

    if (! transport.isPlaying())
    {
        // Static trim: scale the whole existing lane; coalesce the gesture into one undo step.
        if (lane == nullptr || lane->isEmpty()) return;
        auto& st = staticTrims[trackIndex];
        if (st.base == nullptr) st.base = std::make_shared<const engine::AutomationLane> (*lane);
        auto scaled = std::make_shared<engine::AutomationLane> (*st.base);
        const float g = juce::Decibels::decibelsToGain (trimDb);
        for (auto& p : scaled->points) p.value = juce::jlimit (0.0f, 8.0f, p.value * g);
        if (st.issued) session.undo();
        session.execute (std::make_unique<ReplaceAutomationLaneCommand> (trackIndex, scaled, "Trim Volume"));
        st.issued = true;
        if (! gestureActive) staticTrims.erase (trackIndex);
        return;
    }

    const Key key { trackIndex, param };
    auto it = passes.find (key);
    if (it == passes.end())
    {
        Pass pass;
        pass.mode = AutomationMode::trim;
        pass.start = now();
        pass.lastValue = trimDb;
        pass.gestureActive = gestureActive;
        pass.points.push_back ({ pass.start, trimDb });
        passes[key] = std::move (pass);
    }
    else
    {
        auto& pass = it->second;
        pass.gestureActive = gestureActive;
        pass.lastValue = trimDb;
        const auto time = now();
        if (! pass.points.empty() && pass.points.back().time == time) pass.points.back().value = trimDb;
        else pass.points.push_back ({ time, trimDb });
    }
    session.execute (std::make_unique<SetVolumeTrimCommand> (trackIndex, juce::Decibels::decibelsToGain (trimDb)));
}

void AutomationRecorder::parameterChanged (int trackIndex, const engine::ParamId& param, float value, bool gestureActive)
{
    auto* t = session.getTrackOrMaster (trackIndex);
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
    if (param == engine::ParamId::volume()) staticTrims.erase (trackIndex);

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
            for (int i = -1; i < session.getNumTracks(); ++i)
            {
                const auto& t = *session.getTrackOrMaster (i);
                if (t.automationMode != AutomationMode::write) continue;
                for (auto p : { engine::ParamId::volume(), engine::ParamId::pan() })
                    if (! passes.count ({ i, p }) && (i >= 0 || p == engine::ParamId::volume()))
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
    {
        if (pass.mode == AutomationMode::trim) finishTrimPass (key, pass, end);
        else                                   finishPass (key, pass, end);
    }
}

// Bake a trim pass: every existing point inside the pass is scaled by the
// trim at its time, and a point is added at each trim breakpoint so the
// trim curve itself survives. Outside the pass the lane is untouched.
void AutomationRecorder::finishTrimPass (const Key& key, Pass& pass, juce::int64 end)
{
    auto* t = session.getTrackOrMaster (key.track);
    session.execute (std::make_unique<SetVolumeTrimCommand> (key.track, 1.0f));
    if (t == nullptr) return;

    end = juce::jmax (end, pass.start);
    const auto* existing = t->laneFor (key.param);
    const float fallback = currentValue (*t, key.param);

    engine::AutomationLane trimLane;
    trimLane.param = key.param;
    trimLane.points = pass.points;
    if (trimLane.points.empty() || trimLane.points.back().time < end) trimLane.points.push_back ({ end, pass.lastValue });
    trimLane.sortPoints();
    auto trimAt = [&] (juce::int64 time) { return juce::Decibels::decibelsToGain (trimLane.valueAt (time, 0.0f)); };

    auto lane = std::make_shared<engine::AutomationLane>();
    lane->param = key.param;
    auto oldValue = [&] (juce::int64 time) { return existing != nullptr ? existing->valueAt (time, fallback) : fallback; };

    if (existing != nullptr)
        for (const auto& p : existing->points) if (p.time < pass.start) lane->points.push_back (p);
    if (pass.start > 0) lane->points.push_back ({ pass.start - 1, oldValue (pass.start - 1) });

    // Existing points inside the pass, scaled
    if (existing != nullptr)
        for (const auto& p : existing->points)
            if (p.time >= pass.start && p.time <= end) lane->points.push_back ({ p.time, juce::jlimit (0.0f, 8.0f, p.value * trimAt (p.time)) });
    // Trim breakpoints
    for (const auto& tp : trimLane.points)
        if (tp.time >= pass.start && tp.time <= end)
            lane->points.push_back ({ tp.time, juce::jlimit (0.0f, 8.0f, oldValue (tp.time) * trimAt (tp.time)) });

    // Back to the untrimmed data after the pass
    lane->points.push_back ({ end + 1, oldValue (end + 1) });
    if (existing != nullptr)
        for (const auto& p : existing->points) if (p.time > end + 1) lane->points.push_back (p);

    lane->sortPoints();
    // Drop exact duplicate times keeping the last
    std::vector<engine::AutomationPoint> dedup;
    for (const auto& p : lane->points) { if (! dedup.empty() && dedup.back().time == p.time) dedup.back() = p; else dedup.push_back (p); }
    lane->points = std::move (dedup);

    session.execute (std::make_unique<ReplaceAutomationLaneCommand> (key.track, lane, "Trim " + key.param.getName()));
}

void AutomationRecorder::finishPass (const Key& key, Pass& pass, juce::int64 end)
{
    auto* t = session.getTrackOrMaster (key.track);
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
