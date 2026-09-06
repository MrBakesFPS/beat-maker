#include "Freeze.h"
#include "RenderSnapshotBuilder.h"
#include "ClipEdits.h"

namespace beatmaker::model
{

std::vector<int> Freeze::renderableTracks (const Session& s)
{
    std::vector<int> out;
    for (int i = 0; i < s.getNumTracks(); ++i)
        if (s.getTracks()[(size_t) i].carriesAudio()) out.push_back (i);
    return out;
}

std::unique_ptr<engine::RenderSnapshot> Freeze::renderSnapshotForTrack (const Session& s, int trackIndex, int& latencyToTrim)
{
    latencyToTrim = 0;
    auto snapshot = buildRenderSnapshot (s);
    if (! juce::isPositiveAndBelow (trackIndex, (int) snapshot->strips.size())) return snapshot;
    for (int i = 0; i < (int) snapshot->strips.size(); ++i)
    {
        auto& strip = snapshot->strips[(size_t) i];
        if (i != trackIndex) { strip.muted = true; continue; }
        strip.muted = false;
        strip.gain = 1.0f; strip.pan = 0.0f; strip.trimGain = 1.0f;
        strip.sends.clear();
        strip.outputBus = -1;              // straight to the main mix regardless of routing
        strip.outputChannel = 0;
        strip.vcaStrip = -1;
        strip.delaySamples = 0;
        // Keep insert-parameter automation, drop fader/pan/mute lanes
        std::erase_if (strip.automation, [] (const engine::RenderAutomation& a) { return a.lane == nullptr || a.lane->param.type != engine::ParamId::Type::insertParam; });
        for (const auto& ins : strip.inserts)
            if (ins.fx != nullptr && ins.params != nullptr && ! ins.bypass) latencyToTrim += ins.fx->getLatencySamples (*ins.params);
    }
    snapshot->master.inserts.clear();
    snapshot->master.automation.clear();
    snapshot->master.gain = 1.0f;
    snapshot->master.trimGain = 1.0f;
    snapshot->masterGain = 1.0f;
    snapshot->monitors.clear();
    return snapshot;
}

std::unique_ptr<engine::RenderSnapshot> Freeze::stemSnapshot (const Session& s, int trackIndex, bool includeAuxReturns, bool throughMasterInserts)
{
    auto snapshot = buildRenderSnapshot (s);
    const bool stemIsAux = juce::isPositiveAndBelow (trackIndex, (int) snapshot->strips.size()) && snapshot->strips[(size_t) trackIndex].isAux;
    for (int i = 0; i < (int) snapshot->strips.size(); ++i)
    {
        auto& strip = snapshot->strips[(size_t) i];
        if (i == trackIndex) continue;
        if (! strip.isAux)
        {
            // Other sources keep feeding the buses (so aux stems carry their returns) but add nothing to the output.
            strip.outputMuted = true;
        }
        else if (! (includeAuxReturns && ! stemIsAux))
            strip.muted = true;
    }
    if (! throughMasterInserts) snapshot->master.inserts.clear();
    snapshot->monitors.clear();
    return snapshot;
}

std::unique_ptr<Command> Freeze::commitCommand (const Session& s, int trackIndex, std::shared_ptr<const juce::AudioBuffer<float>> rendered,
                                                double sampleRate, const juce::File& file, bool muteSource)
{
    const auto* source = s.getTrack (trackIndex);
    if (source == nullptr || rendered == nullptr || rendered->getNumSamples() == 0) return nullptr;
    auto compound = std::make_unique<CompoundCommand> ("Commit Track");
    if (source->isFrozen()) compound->add (std::make_unique<UnfreezeTrackCommand> (trackIndex));

    Track t;
    t.name = source->name + ".cm";
    t.type = Track::Type::audio;
    t.colour = source->colour;
    t.gain = source->gain; t.pan = source->pan;
    t.sends = source->sends; t.outputBus = source->outputBus; t.outputPath = source->outputPath;
    t.meterType = source->meterType; t.automationMode = source->automationMode;
    for (const auto& lane : source->automation)
        if (lane != nullptr && lane->param.type != engine::ParamId::Type::insertParam) t.automation.push_back (lane);
    AudioClip clip;
    clip.name = source->name;
    clip.sourceFile = file;
    clip.audio = rendered;
    clip.sampleRate = sampleRate;
    clip.timelineStart = 0;
    clip.length = rendered->getNumSamples();
    t.clips.push_back (std::move (clip));
    compound->add (std::make_unique<AddTrackCommand> (std::move (t), trackIndex + 1));
    if (muteSource) compound->add (std::make_unique<SetTrackFlagCommand> (trackIndex, SetTrackFlagCommand::Flag::mute, true));
    return compound;
}

} // namespace beatmaker::model
